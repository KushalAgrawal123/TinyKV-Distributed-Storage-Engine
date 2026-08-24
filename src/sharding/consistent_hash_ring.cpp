#include "tinykv/sharding/consistent_hash_ring.hpp"

#include <algorithm>

namespace tinykv {

uint64_t fnv1a64(const std::string& data) {
  constexpr uint64_t kOffsetBasis = 1469598103934665603ULL;
  constexpr uint64_t kPrime = 1099511628211ULL;
  uint64_t hash = kOffsetBasis;
  for (unsigned char c : data) {
    hash ^= c;
    hash *= kPrime;
  }
  // Plain FNV-1a only fully mixes a byte's influence into the upper bits
  // once several more bytes are hashed after it - for short keys that
  // differ only in their last character or two (e.g. "key1".."key25",
  // exactly what routing keys look like in practice), that leaves the
  // final hash values clustered close together instead of spread across
  // the ring. This finalizer (MurmurHash3's 64-bit mix) forces every
  // input bit to affect every output bit regardless of where it occurred,
  // without changing which hash "wins" a given key - it's applied
  // identically to node and key hashes.
  hash ^= hash >> 33;
  hash *= 0xff51afd7ed558ccdULL;
  hash ^= hash >> 33;
  hash *= 0xc4ceb9fe1a85ec53ULL;
  hash ^= hash >> 33;
  return hash;
}

ConsistentHashRing::ConsistentHashRing(int virtualNodesPerNode) : virtualNodesPerNode_(virtualNodesPerNode) {}

void ConsistentHashRing::addNode(const std::string& nodeId) {
  for (int i = 0; i < virtualNodesPerNode_; ++i) {
    ring_[fnv1a64(nodeId + "#" + std::to_string(i))] = nodeId;
  }
  nodes_.push_back(nodeId);
}

void ConsistentHashRing::removeNode(const std::string& nodeId) {
  for (int i = 0; i < virtualNodesPerNode_; ++i) {
    ring_.erase(fnv1a64(nodeId + "#" + std::to_string(i)));
  }
  nodes_.erase(std::remove(nodes_.begin(), nodes_.end(), nodeId), nodes_.end());
}

std::string ConsistentHashRing::getNode(const std::string& key) const {
  if (ring_.empty()) return "";
  uint64_t hash = fnv1a64(key);
  auto it = ring_.lower_bound(hash);
  if (it == ring_.end()) it = ring_.begin();  // wrap around past the highest position
  return it->second;
}

}  // namespace tinykv
