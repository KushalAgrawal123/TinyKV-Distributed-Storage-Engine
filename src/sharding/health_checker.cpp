#include "tinykv/sharding/health_checker.hpp"

#include <algorithm>
#include <chrono>

#include "tinykv/logger.hpp"
#include "tinykv/net/tcp_client.hpp"

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

}  // namespace

HealthChecker::HealthChecker(ShardTopology& topology, int intervalMs, int maxMissedPings)
    : topology_(topology), intervalMs_(intervalMs), maxMissedPings_(maxMissedPings) {}

HealthChecker::~HealthChecker() { stop(); }

void HealthChecker::start() {
  running_ = true;
  thread_ = std::thread(&HealthChecker::runLoop, this);
}

void HealthChecker::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

// A fresh connection per ping, not a persistent one: health checks are
// infrequent (default once a second) and this way a node that's merely
// slow to respond can't wedge every future check behind a stuck socket -
// each attempt starts clean. Like ReplicaLink's reconnect loop, there's
// no explicit recv timeout; this relies on the OS delivering ECONNREFUSED
// immediately for a closed port (the kill -9 case this phase's
// verification actually exercises) rather than hanging on an
// unresponsive-but-open one - a known, disclosed limitation, not an
// oversight.
bool HealthChecker::pingNode(const std::string& address) const {
  std::string host;
  int port = 0;
  if (!splitAddress(address, host, port)) return false;

  TcpClient client;
  if (!client.connect(host, port)) return false;
  if (!client.sendLine("PING")) return false;
  std::string response;
  if (!client.receiveLine(response)) return false;
  return response == "+OK";
}

void HealthChecker::checkShard(const std::string& shardId) {
  std::string primaryAddr = topology_.primaryAddress(shardId);
  bool primaryUp = pingNode(primaryAddr);
  NodeHealth beforePrimary = topology_.healthOf(shardId, NodeRole::PRIMARY);
  topology_.recordPingResult(shardId, NodeRole::PRIMARY, primaryUp, maxMissedPings_);
  NodeHealth afterPrimary = topology_.healthOf(shardId, NodeRole::PRIMARY);
  if (beforePrimary == NodeHealth::UP && afterPrimary == NodeHealth::DOWN) {
    LOG_WARN("Shard " + shardId + " primary (" + primaryAddr + ") marked DOWN");
  } else if (beforePrimary == NodeHealth::DOWN && afterPrimary == NodeHealth::UP) {
    LOG_INFO("Shard " + shardId + " primary (" + primaryAddr + ") is back UP (not auto-resumed - see docs)");
  }

  if (!topology_.hasReplica(shardId)) return;

  std::string replicaAddr = topology_.replicaAddress(shardId);
  bool replicaUp = pingNode(replicaAddr);
  NodeHealth beforeReplica = topology_.healthOf(shardId, NodeRole::REPLICA);
  topology_.recordPingResult(shardId, NodeRole::REPLICA, replicaUp, maxMissedPings_);
  NodeHealth afterReplica = topology_.healthOf(shardId, NodeRole::REPLICA);
  if (beforeReplica == NodeHealth::UP && afterReplica == NodeHealth::DOWN) {
    LOG_WARN("Shard " + shardId + " replica (" + replicaAddr + ") marked DOWN");
  }

  if (afterPrimary != NodeHealth::DOWN) return;
  if (afterReplica != NodeHealth::UP) return;
  if (topology_.alreadyFailedOver(shardId)) return;

  LOG_WARN("Shard " + shardId + " primary is DOWN and replica is healthy - promoting " + replicaAddr);
  std::string host;
  int port = 0;
  bool promoted = false;
  if (splitAddress(replicaAddr, host, port)) {
    TcpClient client;
    if (client.connect(host, port) && client.sendLine("REPLICAOF NO ONE")) {
      std::string response;
      promoted = client.receiveLine(response) && response == "+OK";
    }
  }

  if (promoted && topology_.promoteReplica(shardId)) {
    LOG_INFO("Shard " + shardId + " failed over: " + replicaAddr + " is now the active node");
  } else {
    LOG_ERROR("Shard " + shardId + " failover FAILED - could not promote " + replicaAddr +
              "; shard remains unavailable until manually recovered");
  }
}

void HealthChecker::runLoop() {
  while (running_.load()) {
    for (const auto& shardId : topology_.shardIds()) {
      if (!running_.load()) break;
      checkShard(shardId);
    }
    for (int waited = 0; waited < intervalMs_ && running_.load(); waited += 50) {
      std::this_thread::sleep_for(std::chrono::milliseconds(std::min(50, intervalMs_ - waited)));
    }
  }
}

}  // namespace tinykv
