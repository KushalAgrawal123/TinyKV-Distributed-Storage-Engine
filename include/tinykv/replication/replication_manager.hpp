#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "tinykv/replication/replica_link.hpp"
#include "tinykv/replication/replica_registry.hpp"

namespace tinykv {

enum class ServerRole { PRIMARY, REPLICA };

// Owns the server's replication state: as a primary, a ReplicaRegistry of
// connected replicas to propagate writes to; as a replica, a ReplicaLink
// pulling from its own primary. REPLICAOF / REPLICAOF NO ONE mutate this
// at runtime, tearing down and rebuilding the ReplicaLink as needed.
//
// The apply callback is set separately via setApplyCallback() rather than
// taken in the constructor: it needs to capture this ReplicationManager
// by reference (to call registry().propagate() for its own downstream
// replicas), which isn't possible before the object exists.
class ReplicationManager {
 public:
  ReplicationManager();
  ~ReplicationManager();

  ReplicationManager(const ReplicationManager&) = delete;
  ReplicationManager& operator=(const ReplicationManager&) = delete;

  void setApplyCallback(ReplicaLink::ApplyCallback callback);

  ServerRole role() const { return role_.load(); }

  // Becomes a replica of host:port, tearing down any previous
  // ReplicaLink first.
  void replicaOf(const std::string& host, int port);

  // Becomes a primary again (stops any active ReplicaLink).
  void replicaOfNoOne();

  // Valid regardless of role - even a replica can have its own
  // downstream replicas.
  ReplicaRegistry& registry() { return registry_; }

 private:
  ReplicaLink::ApplyCallback onApplyReplicatedLine_;
  mutable std::mutex mutex_;
  std::atomic<ServerRole> role_{ServerRole::PRIMARY};
  std::unique_ptr<ReplicaLink> replicaLink_;
  ReplicaRegistry registry_;
};

}  // namespace tinykv
