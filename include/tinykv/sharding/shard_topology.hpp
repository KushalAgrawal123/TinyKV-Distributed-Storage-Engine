#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "tinykv/config.hpp"

namespace tinykv {

enum class NodeRole { PRIMARY, REPLICA };
enum class NodeHealth { UP, DOWN };

// One physical tinykv-server address within a shard, plus the health
// state the HealthChecker maintains for it.
struct ShardNode {
  std::string address;  // host:port
  NodeHealth health = NodeHealth::UP;
  int consecutiveMisses = 0;
};

// A logical shard: a stable id (what the consistent hash ring actually
// routes keys to), its primary, an optional replica, and which physical
// address is currently serving it. activeAddress starts out equal to
// primary.address and is flipped to replica.address by exactly one
// event - HealthChecker promoting the replica after the primary is
// declared DOWN. failedOver latches that so a primary that recovers
// never silently regains traffic (would risk two nodes both believing
// they're authoritative for the same shard) - see docs/PROTOCOL.md.
struct Shard {
  std::string id;
  ShardNode primary;
  bool hasReplica = false;
  ShardNode replica;
  std::string activeAddress;
  bool failedOver = false;
};

// Parsed, mutable view of topology.conf: the fixed shard/primary/replica
// layout plus the runtime health and failover state the HealthChecker
// and Router share. All access goes through one mutex - shard counts are
// small and health checks run at most a few times a second, so this is
// nowhere near a contention hotspot.
class ShardTopology {
 public:
  // Parses topology.conf's flat key=value form into the shard list this
  // takes ownership of. A free function returning std::vector<Shard>
  // (movable - Shard itself holds no mutex) rather than a factory
  // returning ShardTopology by value, since ShardTopology itself holds a
  // std::mutex and so can't be copied or moved once constructed.
  static std::vector<Shard> parseShards(const Config& config);

  explicit ShardTopology(std::vector<Shard> shards) : shards_(std::move(shards)) {}

  ShardTopology(const ShardTopology&) = delete;
  ShardTopology& operator=(const ShardTopology&) = delete;

  std::vector<std::string> shardIds() const;

  // The address currently serving shardId - the primary's address until
  // (if ever) a failover promotes the replica. Empty if shardId is
  // unknown.
  std::string activeAddressFor(const std::string& shardId) const;

  bool hasReplica(const std::string& shardId) const;
  std::string primaryAddress(const std::string& shardId) const;
  std::string replicaAddress(const std::string& shardId) const;
  bool alreadyFailedOver(const std::string& shardId) const;

  // Records the outcome of one health-check ping against shardId's
  // primary or replica node, updating its consecutive-miss count and
  // UP/DOWN health accordingly.
  void recordPingResult(const std::string& shardId, NodeRole role, bool success, int maxMissedPings);

  NodeHealth healthOf(const std::string& shardId, NodeRole role) const;

  // Promotes shardId's replica to active. Returns false if the shard has
  // no replica or has already been failed over (caller should not have
  // called this - included as a defensive guard, not a normal control
  // path).
  bool promoteReplica(const std::string& shardId);

  struct NodeSnapshot {
    std::string shardId;
    NodeRole role;
    std::string address;
    NodeHealth health;
    bool active;
  };
  // One entry per physical node (primary always, replica if configured)
  // across every shard - the direct source for the NODES command and for
  // the HealthChecker's ping loop, both of which need to iterate nodes
  // without holding the lock during network I/O.
  std::vector<NodeSnapshot> snapshot() const;

 private:
  mutable std::mutex mutex_;
  std::vector<Shard> shards_;

  Shard* findShard(const std::string& shardId);
  const Shard* findShard(const std::string& shardId) const;
};

}  // namespace tinykv
