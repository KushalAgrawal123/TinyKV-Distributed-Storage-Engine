#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "tinykv/sharding/shard_topology.hpp"

namespace tinykv {

// Background thread that periodically PINGs every physical node in a
// ShardTopology and drives single-coordinator failover - the actual
// point of Phase 10C. Every intervalMs, each shard's primary and (if
// configured) replica get one fresh, short-lived connection carrying a
// single PING; consecutive failures beyond maxMissedPings mark a node
// DOWN, a single success marks it back UP (see
// ShardTopology::recordPingResult).
//
// When a shard's primary is found DOWN and that shard has a replica that
// is (a) UP and (b) not already promoted, this issues REPLICAOF NO ONE
// to the replica and flips the shard's active address to it. This is
// explicitly single-coordinator failover - this router is the sole
// authority, there's no quorum/consensus - and it is one-way: a primary
// that comes back UP is still just reported as UP in NODES, never
// automatically resumes serving traffic. Auto-reverting would risk two
// nodes both believing they're authoritative for the same shard
// (split-brain); the operator must explicitly REPLICAOF the recovered
// primary back in themselves.
class HealthChecker {
 public:
  HealthChecker(ShardTopology& topology, int intervalMs, int maxMissedPings);
  ~HealthChecker();

  void start();
  void stop();

 private:
  void runLoop();
  bool pingNode(const std::string& address) const;
  void checkShard(const std::string& shardId);

  ShardTopology& topology_;
  int intervalMs_;
  int maxMissedPings_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace tinykv
