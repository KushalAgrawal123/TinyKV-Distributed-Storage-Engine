#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "tinykv/net/tcp_client.hpp"
#include "tinykv/sharding/consistent_hash_ring.hpp"

namespace tinykv {

// One persistent connection to a backend shard, guarded by its own mutex.
// The wire protocol is strictly request/response with no pipelining, so
// concurrent router connection-handler threads routing to the same shard
// must take turns on that shard's single connection - the mutex serializes
// them (a real bottleneck under heavy load on one shard, but correct, and
// consistent with the project's "simple over fast" persistence/
// replication designs).
struct BackendConnection {
  std::mutex mutex;
  TcpClient client;
  std::string host;
  int port = 0;
};

// Reverse proxy in front of a fixed set of ordinary tinykv-server shards.
// Backends are unmodified tinykv-server instances - all sharding logic
// (which key goes where) lives here, so a plain `nc`/tinykv-cli can still
// talk directly to any individual shard for verification.
class Router {
 public:
  explicit Router(const std::vector<std::string>& backendAddresses);

  // Handles one client connection end-to-end: reads request lines,
  // extracts the routing key, forwards to the right backend (opening or
  // reconnecting its persistent connection on demand), and relays the
  // reply back. Returns when the client disconnects.
  void handleConnection(int clientFd);

  // Which backend "host:port" a key currently maps to - shared by ROUTE
  // and the actual forwarding path so the two can never disagree.
  std::string nodeForKey(const std::string& key) const;

  size_t backendCount() const { return ring_.nodeCount(); }

 private:
  BackendConnection& backendFor(const std::string& nodeId);
  bool forward(const std::string& nodeId, const std::string& line, std::string& outResponse);

  ConsistentHashRing ring_;
  std::mutex backendsMutex_;  // guards backends_'s structure only, not any one connection's traffic
  std::unordered_map<std::string, std::unique_ptr<BackendConnection>> backends_;
};

}  // namespace tinykv
