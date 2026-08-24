#include "tinykv/sharding/shard_topology.hpp"

#include <sstream>

namespace tinykv {

namespace {

std::vector<std::string> splitCsv(const std::string& csv) {
  std::vector<std::string> parts;
  std::istringstream iss(csv);
  std::string token;
  while (std::getline(iss, token, ',')) {
    if (!token.empty()) parts.push_back(token);
  }
  return parts;
}

}  // namespace

std::vector<Shard> ShardTopology::parseShards(const Config& config) {
  std::vector<Shard> shards;
  for (const auto& shardId : splitCsv(config.getString("shards", ""))) {
    Shard shard;
    shard.id = shardId;
    shard.primary.address = config.getString(shardId + ".primary", "");
    std::string replicaAddr = config.getString(shardId + ".replica", "");
    shard.hasReplica = !replicaAddr.empty();
    shard.replica.address = replicaAddr;
    shard.activeAddress = shard.primary.address;
    shards.push_back(std::move(shard));
  }
  return shards;
}

Shard* ShardTopology::findShard(const std::string& shardId) {
  for (auto& shard : shards_) {
    if (shard.id == shardId) return &shard;
  }
  return nullptr;
}

const Shard* ShardTopology::findShard(const std::string& shardId) const {
  for (const auto& shard : shards_) {
    if (shard.id == shardId) return &shard;
  }
  return nullptr;
}

std::vector<std::string> ShardTopology::shardIds() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> ids;
  ids.reserve(shards_.size());
  for (const auto& shard : shards_) ids.push_back(shard.id);
  return ids;
}

std::string ShardTopology::activeAddressFor(const std::string& shardId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const Shard* shard = findShard(shardId);
  return shard != nullptr ? shard->activeAddress : "";
}

bool ShardTopology::hasReplica(const std::string& shardId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const Shard* shard = findShard(shardId);
  return shard != nullptr && shard->hasReplica;
}

std::string ShardTopology::primaryAddress(const std::string& shardId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const Shard* shard = findShard(shardId);
  return shard != nullptr ? shard->primary.address : "";
}

std::string ShardTopology::replicaAddress(const std::string& shardId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const Shard* shard = findShard(shardId);
  return shard != nullptr ? shard->replica.address : "";
}

bool ShardTopology::alreadyFailedOver(const std::string& shardId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const Shard* shard = findShard(shardId);
  return shard != nullptr && shard->failedOver;
}

void ShardTopology::recordPingResult(const std::string& shardId, NodeRole role, bool success, int maxMissedPings) {
  std::lock_guard<std::mutex> lock(mutex_);
  Shard* shard = findShard(shardId);
  if (shard == nullptr) return;
  ShardNode& node = (role == NodeRole::PRIMARY) ? shard->primary : shard->replica;

  if (success) {
    node.consecutiveMisses = 0;
    node.health = NodeHealth::UP;
  } else {
    ++node.consecutiveMisses;
    if (node.consecutiveMisses >= maxMissedPings) {
      node.health = NodeHealth::DOWN;
    }
  }
}

NodeHealth ShardTopology::healthOf(const std::string& shardId, NodeRole role) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const Shard* shard = findShard(shardId);
  if (shard == nullptr) return NodeHealth::DOWN;
  return (role == NodeRole::PRIMARY) ? shard->primary.health : shard->replica.health;
}

bool ShardTopology::promoteReplica(const std::string& shardId) {
  std::lock_guard<std::mutex> lock(mutex_);
  Shard* shard = findShard(shardId);
  if (shard == nullptr || !shard->hasReplica || shard->failedOver) return false;
  shard->activeAddress = shard->replica.address;
  shard->failedOver = true;
  return true;
}

std::vector<ShardTopology::NodeSnapshot> ShardTopology::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<NodeSnapshot> nodes;
  for (const auto& shard : shards_) {
    nodes.push_back({shard.id, NodeRole::PRIMARY, shard.primary.address, shard.primary.health,
                      shard.activeAddress == shard.primary.address});
    if (shard.hasReplica) {
      nodes.push_back({shard.id, NodeRole::REPLICA, shard.replica.address, shard.replica.health,
                        shard.activeAddress == shard.replica.address});
    }
  }
  return nodes;
}

}  // namespace tinykv
