#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tinykv {

// Tracks per-key TTLs and fires a callback when they expire, via a single
// background thread that sleeps until the next real deadline rather than
// busy-polling. A key's TTL can be rescheduled or cancelled before its
// old deadline arrives; rather than an indexed/updatable heap, the old
// heap entry is simply left in place and discarded lazily when it's
// popped, by checking it against currentExpiry_ (the source of truth for
// "what's this key's live deadline right now").
class ExpiryManager {
 public:
  using ExpireCallback = std::function<void(const std::string& key)>;

  ExpiryManager();
  ~ExpiryManager();

  ExpiryManager(const ExpiryManager&) = delete;
  ExpiryManager& operator=(const ExpiryManager&) = delete;

  // Schedules key to expire at expiresAt, replacing any existing
  // schedule for it.
  void schedule(const std::string& key, std::chrono::steady_clock::time_point expiresAt);

  // Cancels any pending expiration for key (used by PERSIST, and by a
  // plain SET/DEL that should clear an old TTL).
  void cancel(const std::string& key);

  // The key's current expiry deadline, if it has an active one.
  std::optional<std::chrono::steady_clock::time_point> expiryOf(const std::string& key) const;

  // Starts the background sweeper thread. onExpire(key) is invoked (off
  // the caller's thread, with no locks held) for each key whose deadline
  // arrives while it's still the key's live schedule.
  void start(ExpireCallback onExpire);
  void stop();

 private:
  struct HeapEntry {
    std::chrono::steady_clock::time_point when;
    std::string key;
  };
  struct HeapEntryCompare {
    // std::priority_queue is a max-heap by default; flip the comparison
    // so the earliest deadline sorts to the top.
    bool operator()(const HeapEntry& a, const HeapEntry& b) const { return a.when > b.when; }
  };

  void sweepLoop(ExpireCallback onExpire);

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapEntryCompare> heap_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> currentExpiry_;
  std::thread worker_;
  bool stop_ = false;
};

}  // namespace tinykv
