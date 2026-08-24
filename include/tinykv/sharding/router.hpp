#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "tinykv/net/tcp_client.hpp"
#include "tinykv/sharding/consistent_hash_ring.hpp"
#include "tinykv/sharding/shard_topology.hpp"

namespace tinykv {

// One persistent connection to whichever physical node is currently
// serving a shard, guarded by its own mutex. The wire protocol is
// strictly request/response with no pipelining, so concurrent router
// connection-handler threads routing to the same shard must take turns -
// the mutex serializes them. `address` records which node the
// connection actually points at, so forward() can tell when a failover
// has moved the shard's active address and reconnect to the new one.
struct BackendConnection {
  std::mutex mutex;
  TcpClient client;
  std::string address;
};

// Reverse proxy in front of a fixed set of ordinary tinykv-server shards,
// each optionally backed by a replica (see ShardTopology/HealthChecker
// for the failover side of this). Backends are unmodified tinykv-server
// instances - all sharding and failover logic lives here, so a plain
// `nc`/tinykv-cli can still talk directly to any individual node for
// verification.
//
// Keys are hashed to a stable logical shard id (ConsistentHashRing),
// never to a physical address directly - which physical node currently
// serves a shard can change (failover) without changing which shard owns
// a given key, so the two concerns are kept separate on purpose.
class Router {
 public:
  explicit Router(ShardTopology& topology);

  // Handles one client connection end-to-end: reads request lines,
  // extracts the routing key, forwards to the right shard's currently
  // active node (opening or reconnecting its persistent connection on
  // demand), and relays the reply back. Returns when the client
  // disconnects.
  void handleConnection(int clientFd);

  // The physical "host:port" currently serving key - shared by ROUTE and
  // the actual forwarding path so the two can never disagree.
  std::string addressForKey(const std::string& key) const;

  size_t shardCount() const { return ring_.nodeCount(); }

 private:
  BackendConnection& backendFor(const std::string& shardId);
  bool forward(const std::string& shardId, const std::string& line, std::string& outResponse);
  std::string nodesReply() const;

  ShardTopology& topology_;
  ConsistentHashRing ring_;
  std::mutex backendsMutex_;  // guards backends_'s structure only, not any one connection's traffic
  std::unordered_map<std::string, std::unique_ptr<BackendConnection>> backends_;
};

}  // namespace tinykv
