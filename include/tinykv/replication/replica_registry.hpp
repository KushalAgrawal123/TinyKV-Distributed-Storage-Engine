#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace tinykv {

// Thread-safe list of connected replica sockets, on the primary side.
// After every successful write, its exact request line is sent to each
// registered replica (see PersistenceManager::recordWrite for the
// analogous AOF concept - propagation reuses the same "line" idea).
class ReplicaRegistry {
 public:
  void add(int fd);
  void remove(int fd);

  // Sends line to every currently-registered replica. Best-effort: a
  // write() failing (e.g. a replica just disconnected) is silently
  // ignored here rather than treated as an error - that replica's own
  // connection-handling thread will notice the disconnect via its
  // read loop and call remove().
  void propagate(const std::string& line);

  size_t size() const;

 private:
  mutable std::mutex mutex_;
  std::vector<int> replicaFds_;
};

}  // namespace tinykv
