#include "tinykv/replication/replica_registry.hpp"

#include <algorithm>

#include "tinykv/net/line_protocol.hpp"

namespace tinykv {

void ReplicaRegistry::add(int fd) {
  std::lock_guard<std::mutex> lock(mutex_);
  replicaFds_.push_back(fd);
}

void ReplicaRegistry::remove(int fd) {
  std::lock_guard<std::mutex> lock(mutex_);
  replicaFds_.erase(std::remove(replicaFds_.begin(), replicaFds_.end(), fd), replicaFds_.end());
}

void ReplicaRegistry::propagate(const std::string& line) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (int fd : replicaFds_) {
    writeLine(fd, line);
  }
}

size_t ReplicaRegistry::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return replicaFds_.size();
}

}  // namespace tinykv
