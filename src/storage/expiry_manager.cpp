#include "tinykv/storage/expiry_manager.hpp"

namespace tinykv {

ExpiryManager::ExpiryManager() = default;

ExpiryManager::~ExpiryManager() { stop(); }

void ExpiryManager::schedule(const std::string& key, std::chrono::steady_clock::time_point expiresAt) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    currentExpiry_[key] = expiresAt;
    heap_.push({expiresAt, key});
  }
  // Wakes the sweeper in case this deadline is sooner than whatever it
  // was already waiting on.
  cv_.notify_one();
}

void ExpiryManager::cancel(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  currentExpiry_.erase(key);
  // The heap entry (if any) is left in place - it becomes stale and is
  // discarded lazily when sweepLoop eventually pops it.
}

std::optional<std::chrono::steady_clock::time_point> ExpiryManager::expiryOf(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = currentExpiry_.find(key);
  if (it == currentExpiry_.end()) return std::nullopt;
  return it->second;
}

void ExpiryManager::start(ExpireCallback onExpire) {
  worker_ = std::thread([this, onExpire] { sweepLoop(onExpire); });
}

void ExpiryManager::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void ExpiryManager::sweepLoop(ExpireCallback onExpire) {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stop_) {
    if (heap_.empty()) {
      cv_.wait(lock);
      continue;
    }

    auto deadline = heap_.top().when;
    // No predicate here on purpose: a notify_one() from schedule() should
    // just wake us up so we can re-check the (possibly now-earlier) top
    // of the heap, not be swallowed because stop_ is still false.
    if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
      if (heap_.empty()) continue;

      HeapEntry entry = heap_.top();
      heap_.pop();

      auto it = currentExpiry_.find(entry.key);
      bool isLive = (it != currentExpiry_.end() && it->second == entry.when);
      if (isLive) {
        currentExpiry_.erase(it);
        lock.unlock();
        onExpire(entry.key);
        lock.lock();
      }
      // else: a stale entry superseded by a later cancel()/schedule() -
      // just discard it.
    }
    // else: woken early (a new/earlier schedule, or stop()) - loop back
    // around and re-check stop_ and the current heap top.
  }
}

}  // namespace tinykv
