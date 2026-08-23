#include "tinykv/replication/replication_manager.hpp"

#include "tinykv/logger.hpp"

namespace tinykv {

ReplicationManager::ReplicationManager() = default;

ReplicationManager::~ReplicationManager() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (replicaLink_) {
    replicaLink_->stop();
  }
}

void ReplicationManager::setApplyCallback(ReplicaLink::ApplyCallback callback) {
  onApplyReplicatedLine_ = std::move(callback);
}

void ReplicationManager::replicaOf(const std::string& host, int port) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (replicaLink_) {
    replicaLink_->stop();
    replicaLink_.reset();
  }
  role_ = ServerRole::REPLICA;
  replicaLink_ = std::make_unique<ReplicaLink>(host, port, onApplyReplicatedLine_);
  replicaLink_->start();
  LOG_INFO("Now a replica of " + host + ":" + std::to_string(port));
}

void ReplicationManager::replicaOfNoOne() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (replicaLink_) {
    replicaLink_->stop();
    replicaLink_.reset();
  }
  role_ = ServerRole::PRIMARY;
  LOG_INFO("Now a primary (REPLICAOF NO ONE)");
}

}  // namespace tinykv
