#include "tinykv/storage/kv_store.hpp"

#include <stdexcept>

namespace tinykv {

KVStore::KVStore(size_t capacity) : capacity_(capacity) {}

void KVStore::set(const std::string& key, const std::string& value) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  setLocked(key, value);
}

void KVStore::setLocked(const std::string& key, const std::string& value) {
  auto it = index_.find(key);
  if (it != index_.end()) {
    it->second->second = value;
    lru_.splice(lru_.begin(), lru_, it->second);
    return;
  }

  lru_.emplace_front(key, value);
  index_[key] = lru_.begin();

  if (capacity_ != 0) {
    while (lru_.size() > capacity_) {
      index_.erase(lru_.back().first);
      lru_.pop_back();
    }
  }
}

std::optional<std::string> KVStore::get(const std::string& key) const {
  // A GET is logically a read, but touching LRU recency mutates lru_, so
  // this needs the exclusive lock rather than a shared_lock - an
  // accepted trade-off (see the Phase 7 plan notes) that costs some read
  // concurrency in exchange for real O(1) LRU tracking.
  std::unique_lock<std::shared_mutex> lock(mutex_);
  auto it = index_.find(key);
  if (it == index_.end()) return std::nullopt;
  lru_.splice(lru_.begin(), lru_, it->second);
  return it->second->second;
}

bool KVStore::del(const std::string& key) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  auto it = index_.find(key);
  if (it == index_.end()) return false;
  lru_.erase(it->second);
  index_.erase(it);
  return true;
}

bool KVStore::exists(const std::string& key) const {
  // Unlike get(), an existence check doesn't count as an access for LRU
  // purposes (matching Redis), so this stays a real concurrent read.
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return index_.find(key) != index_.end();
}

size_t KVStore::size() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return index_.size();
}

long long KVStore::incrementBy(const std::string& key, long long delta) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  long long current = 0;
  auto it = index_.find(key);
  if (it != index_.end()) {
    try {
      current = std::stoll(it->second->second);
    } catch (...) {
      throw std::invalid_argument("value is not an integer");
    }
  }

  long long updated = current + delta;
  setLocked(key, std::to_string(updated));
  return updated;
}

}  // namespace tinykv
