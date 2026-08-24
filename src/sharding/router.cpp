#include "tinykv/sharding/router.hpp"

#include "tinykv/net/line_protocol.hpp"
#include "tinykv/protocol/command.hpp"
#include "tinykv/protocol/parser.hpp"
#include "tinykv/protocol/reply.hpp"

namespace tinykv {

Router::Router(const std::vector<std::string>& backendAddresses) {
  for (const auto& addr : backendAddresses) {
    ring_.addNode(addr);
  }
}

std::string Router::nodeForKey(const std::string& key) const { return ring_.getNode(key); }

BackendConnection& Router::backendFor(const std::string& nodeId) {
  std::lock_guard<std::mutex> lock(backendsMutex_);
  auto it = backends_.find(nodeId);
  if (it != backends_.end()) return *it->second;

  auto conn = std::make_unique<BackendConnection>();
  size_t colon = nodeId.rfind(':');
  conn->host = nodeId.substr(0, colon);
  conn->port = std::stoi(nodeId.substr(colon + 1));
  BackendConnection& ref = *conn;
  backends_.emplace(nodeId, std::move(conn));
  return ref;
}

bool Router::forward(const std::string& nodeId, const std::string& line, std::string& outResponse) {
  BackendConnection& backend = backendFor(nodeId);
  std::lock_guard<std::mutex> lock(backend.mutex);
  if (!backend.client.isConnected() && !backend.client.connect(backend.host, backend.port)) {
    return false;
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
      std::string node = nodeForKey(cmd.args[0]);
      if (!writeLine(clientFd, Reply::bulk(node))) break;
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
    std::string node = nodeForKey(cmd.args[0]);
    std::string response;
    if (!forward(node, line, response)) {
      if (!writeLine(clientFd, Reply::error("shard " + node + " unavailable"))) break;
      continue;
    }
    if (!writeLine(clientFd, response)) break;
  }
}

}  // namespace tinykv
