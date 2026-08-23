#pragma once

#include <list>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace tinykv {

class KVStore {
 public:
  // capacity == 0 means unbounded (no LRU eviction).
  explicit KVStore(size_t capacity = 0);

  void set(const std::string& key, const std::string& value);
  std::optional<std::string> get(const std::string& key) const;
  bool del(const std::string& key);
  bool exists(const std::string& key) const;
  size_t size() const;

  // Atomically parses the current value as an integer (a missing key
  // reads as 0), adds delta, stores the result back as a string, and
  // returns the new value - all under one write lock, so concurrent
  // INCR/DECR calls can't lose an update the way a separate GET-then-SET
  // from the command layer would. Throws std::invalid_argument if the
  // existing value isn't a valid integer.
  long long incrementBy(const std::string& key, long long delta);

 private:
  using Entry = std::pair<std::string, std::string>;  // key, value
  using EntryList = std::list<Entry>;                 // front = most recently used

  // Assumes mutex_ is already held exclusively. Creates or updates key,
  // moving it to the front of the LRU list, then evicts from the back
  // if that insertion pushed the store over capacity.
  void setLocked(const std::string& key, const std::string& value);

  mutable std::shared_mutex mutex_;
  size_t capacity_;

  // lru_/index_ are mutable because get() - logically a read - still
  // needs to update recency order; see the note in kv_store.cpp.
  mutable EntryList lru_;
  mutable std::unordered_map<std::string, EntryList::iterator> index_;
};

}  // namespace tinykv
