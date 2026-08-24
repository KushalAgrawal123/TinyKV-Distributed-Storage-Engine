#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tinykv {

// FNV-1a 64-bit hash. Used instead of std::hash<std::string>, whose output
// is implementation-defined and not guaranteed stable across processes or
// runs - every router/shard process must independently compute the exact
// same hash for the same key for routing to be consistent at all.
uint64_t fnv1a64(const std::string& data);

// Maps keys to physical nodes via consistent hashing: each physical node
// is given several hash positions ("virtual nodes") scattered around a
// hash ring, and a key belongs to whichever node's nearest virtual node
// comes next clockwise from the key's own hash. Compared to naive
// hash(key) % N, adding or removing a node only remaps the fraction of
// keys that fell between that node's virtual positions and their
// neighbors, not the whole keyspace - the actual point of building this
// instead of the simpler alternative.
class ConsistentHashRing {
 public:
  explicit ConsistentHashRing(int virtualNodesPerNode = 150);

  void addNode(const std::string& nodeId);
  void removeNode(const std::string& nodeId);

  // Returns the node responsible for key, or "" if the ring has no nodes.
  std::string getNode(const std::string& key) const;

  size_t nodeCount() const { return nodes_.size(); }

 private:
  int virtualNodesPerNode_;
  std::map<uint64_t, std::string> ring_;  // hash position -> node id, sorted
  std::vector<std::string> nodes_;        // physical nodes currently in the ring
};

}  // namespace tinykv
