#include "tinykv/sharding/router.hpp"

#include <sstream>

#include "tinykv/net/line_protocol.hpp"
#include "tinykv/protocol/command.hpp"
#include "tinykv/protocol/parser.hpp"
#include "tinykv/protocol/reply.hpp"

namespace tinykv {

namespace {

bool splitAddress(const std::string& address, std::string& host, int& port) {
  size_t colon = address.rfind(':');
  if (colon == std::string::npos) return false;
  host = address.substr(0, colon);
  try {
    port = std::stoi(address.substr(colon + 1));
  } catch (...) {
    return false;
  }
  return true;
}

const char* roleName(NodeRole role) { return role == NodeRole::PRIMARY ? "PRIMARY" : "REPLICA"; }
const char* healthName(NodeHealth health) { return health == NodeHealth::UP ? "UP" : "DOWN"; }

}  // namespace

Router::Router(ShardTopology& topology) : topology_(topology) {
  for (const auto& shardId : topology_.shardIds()) {
    ring_.addNode(shardId);
  }
}

std::string Router::addressForKey(const std::string& key) const {
  std::string shardId = ring_.getNode(key);
  return topology_.activeAddressFor(shardId);
}

BackendConnection& Router::backendFor(const std::string& shardId) {
  std::lock_guard<std::mutex> lock(backendsMutex_);
  auto it = backends_.find(shardId);
  if (it != backends_.end()) return *it->second;

  auto conn = std::make_unique<BackendConnection>();
  BackendConnection& ref = *conn;
  backends_.emplace(shardId, std::move(conn));
  return ref;
}

bool Router::forward(const std::string& shardId, const std::string& line, std::string& outResponse) {
  std::string address = topology_.activeAddressFor(shardId);
  if (address.empty()) return false;

  BackendConnection& backend = backendFor(shardId);
  std::lock_guard<std::mutex> lock(backend.mutex);

  if (backend.address != address) {
    // The shard's active node changed - most likely a failover promoted
    // its replica. Drop any connection to the old node; the block below
    // opens a fresh one to the new address.
    backend.client.close();
    backend.address = address;
  }

  if (!backend.client.isConnected()) {
    std::string host;
    int port = 0;
    if (!splitAddress(address, host, port) || !backend.client.connect(host, port)) {
      return false;
    }
  }
  if (!backend.client.sendLine(line) || !backend.client.receiveLine(outResponse)) {
    // Drop the dead connection so the next request against this shard
    // reconnects from scratch instead of repeatedly failing on a socket
    // that's already gone.
    backend.client.close();
    return false;
  }
  return true;
}

std::string Router::nodesReply() const {
  std::ostringstream out;
  bool first = true;
  for (const auto& node : topology_.snapshot()) {
    if (!first) out << ";";
    first = false;
    out << node.shardId << ":" << roleName(node.role) << ":" << node.address << ":" << healthName(node.health) << ":"
        << (node.active ? "ACTIVE" : "STANDBY");
  }
  return out.str();
}

void Router::handleConnection(int clientFd) {
  LineReader reader;
  std::string line;
  while (reader.readLine(clientFd, line)) {
    if (line.empty()) continue;

    Command cmd;
    try {
      cmd = Parser::parse(line);
    } catch (const ProtocolError& e) {
      if (!writeLine(clientFd, Reply::error(e.what()))) break;
      continue;
    }

    if (cmd.type == CommandType::ROUTE) {
      if (cmd.args.size() != 1) {
        if (!writeLine(clientFd, Reply::error("wrong number of arguments for 'ROUTE'"))) break;
        continue;
      }
      std::string address = addressForKey(cmd.args[0]);
      if (!writeLine(clientFd, Reply::bulk(address))) break;
      continue;
    }

    if (cmd.type == CommandType::NODES) {
      if (!cmd.args.empty()) {
        if (!writeLine(clientFd, Reply::error("wrong number of arguments for 'NODES'"))) break;
        continue;
      }
      if (!writeLine(clientFd, Reply::bulk(nodesReply()))) break;
      continue;
    }

    // PING carries no key, so there's nothing to route it by - answer it
    // locally rather than forwarding to an arbitrary shard.
    if (cmd.type == CommandType::PING && cmd.args.empty()) {
      if (!writeLine(clientFd, Reply::ok())) break;
      continue;
    }

    if (cmd.args.empty()) {
      if (!writeLine(clientFd, Reply::error("'" + cmd.name + "' has no key to route on"))) break;
      continue;
    }

    // By convention every routable command's key is its first argument
    // (SET/GET/DEL/INCR/DECR/TTL/EXPIRE/PERSIST all agree on this), so the
    // router never needs command-specific knowledge of where the key sits.
    std::string shardId = ring_.getNode(cmd.args[0]);
    std::string response;
    if (!forward(shardId, line, response)) {
      if (!writeLine(clientFd, Reply::error("shard " + shardId + " unavailable"))) break;
      continue;
    }
    if (!writeLine(clientFd, response)) break;
  }
}

}  // namespace tinykv
