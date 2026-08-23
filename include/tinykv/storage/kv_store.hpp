#pragma once

#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace tinykv {

class KVStore {
 public:
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
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, std::string> data_;
};

}  // namespace tinykv
