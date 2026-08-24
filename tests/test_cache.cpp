#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "tinykv/storage/expiry_manager.hpp"
#include "tinykv/storage/kv_store.hpp"

using tinykv::ExpiryManager;
using tinykv::KVStore;

// --- LRU eviction (KVStore with a bounded capacity) ---

TEST(LruCacheTest, UnboundedStoreNeverEvicts) {
  KVStore store(0);
  for (int i = 0; i < 100; ++i) {
    store.set("key" + std::to_string(i), "v");
  }
  EXPECT_EQ(store.size(), 100u);
}

TEST(LruCacheTest, EvictsLeastRecentlyUsedOnOverflow) {
  KVStore store(3);
  store.set("a", "1");
  store.set("b", "2");
  store.set("c", "3");
  store.set("d", "4");  // "a" was never touched again - it should go

  EXPECT_EQ(store.size(), 3u);
  EXPECT_FALSE(store.exists("a"));
  EXPECT_TRUE(store.exists("b"));
  EXPECT_TRUE(store.exists("c"));
  EXPECT_TRUE(store.exists("d"));
}

TEST(LruCacheTest, TouchingAKeyViaGetProtectsItFromEviction) {
  KVStore store(3);
  store.set("a", "1");
  store.set("b", "2");
  store.set("c", "3");
  store.get("a");  // "b" is now the least-recently-used, not "a"
  store.set("d", "4");

  EXPECT_TRUE(store.exists("a"));
  EXPECT_FALSE(store.exists("b"));
  EXPECT_TRUE(store.exists("c"));
  EXPECT_TRUE(store.exists("d"));
}

TEST(LruCacheTest, OverwritingAnExistingKeyCountsAsATouch) {
  KVStore store(2);
  store.set("a", "1");
  store.set("b", "2");
  store.set("a", "updated");  // touches "a"
  store.set("c", "3");        // should evict "b", not "a"

  EXPECT_TRUE(store.exists("a"));
  EXPECT_FALSE(store.exists("b"));
  EXPECT_TRUE(store.exists("c"));
  EXPECT_EQ(*store.get("a"), "updated");
}

// --- TTL scheduling & expiration (ExpiryManager) ---

TEST(ExpiryManagerTest, ScheduleThenExpiryOfReportsTheDeadline) {
  ExpiryManager expiry;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  expiry.schedule("token", deadline);
  auto got = expiry.expiryOf("token");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(*got, deadline);
}

TEST(ExpiryManagerTest, CancelRemovesThePendingSchedule) {
  ExpiryManager expiry;
  expiry.schedule("token", std::chrono::steady_clock::now() + std::chrono::seconds(60));
  expiry.cancel("token");
  EXPECT_FALSE(expiry.expiryOf("token").has_value());
}

TEST(ExpiryManagerTest, KeyWithNoScheduleHasNoExpiry) {
  ExpiryManager expiry;
  EXPECT_FALSE(expiry.expiryOf("nosuchkey").has_value());
}

TEST(ExpiryManagerTest, FiresCallbackWhenDeadlineArrives) {
  ExpiryManager expiry;
  std::mutex m;
  std::condition_variable cv;
  std::vector<std::string> expired;
  expiry.start([&](const std::string& key) {
    std::lock_guard<std::mutex> lock(m);
    expired.push_back(key);
    cv.notify_all();
  });

  expiry.schedule("soon", std::chrono::steady_clock::now() + std::chrono::milliseconds(100));

  std::unique_lock<std::mutex> lock(m);
  bool fired = cv.wait_for(lock, std::chrono::seconds(2), [&] { return !expired.empty(); });
  ASSERT_TRUE(fired);
  EXPECT_EQ(expired[0], "soon");
  lock.unlock();
  expiry.stop();
}

// Exercises the lazy-discard heap: an earlier (now-stale) heap entry for
// the same key must not fire once the key has been rescheduled later.
TEST(ExpiryManagerTest, ReschedulingBeforeTheOldDeadlineUsesTheNewOne) {
  ExpiryManager expiry;
  std::mutex m;
  std::condition_variable cv;
  std::vector<std::string> expired;
  expiry.start([&](const std::string& key) {
    std::lock_guard<std::mutex> lock(m);
    expired.push_back(key);
    cv.notify_all();
  });

  auto start = std::chrono::steady_clock::now();
  expiry.schedule("key", start + std::chrono::milliseconds(50));
  expiry.schedule("key", start + std::chrono::milliseconds(400));  // push the deadline back

  std::unique_lock<std::mutex> lock(m);
  bool firedEarly = cv.wait_for(lock, std::chrono::milliseconds(200), [&] { return !expired.empty(); });
  EXPECT_FALSE(firedEarly) << "fired at the original (superseded) deadline instead of the rescheduled one";

  bool firedLater = cv.wait_for(lock, std::chrono::seconds(2), [&] { return !expired.empty(); });
  ASSERT_TRUE(firedLater);
  EXPECT_EQ(expired.size(), 1u) << "should have fired exactly once, not once per stale heap entry";
  lock.unlock();
  expiry.stop();
}

TEST(ExpiryManagerTest, CancelBeforeDeadlineMeansItNeverFires) {
  ExpiryManager expiry;
  std::mutex m;
  std::condition_variable cv;
  std::vector<std::string> expired;
  expiry.start([&](const std::string& key) {
    std::lock_guard<std::mutex> lock(m);
    expired.push_back(key);
    cv.notify_all();
  });

  expiry.schedule("cancelme", std::chrono::steady_clock::now() + std::chrono::milliseconds(150));
  expiry.cancel("cancelme");

  std::unique_lock<std::mutex> lock(m);
  bool fired = cv.wait_for(lock, std::chrono::milliseconds(500), [&] { return !expired.empty(); });
  EXPECT_FALSE(fired);
  lock.unlock();
  expiry.stop();
}
