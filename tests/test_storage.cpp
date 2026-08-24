#include <gtest/gtest.h>

#include <map>
#include <stdexcept>

#include "tinykv/storage/kv_store.hpp"

using tinykv::KVStore;

TEST(KVStoreTest, SetAndGetRoundTrips) {
  KVStore store;
  store.set("foo", "bar");
  auto value = store.get("foo");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "bar");
}

TEST(KVStoreTest, GetOnMissingKeyReturnsNullopt) {
  KVStore store;
  EXPECT_FALSE(store.get("nope").has_value());
}

TEST(KVStoreTest, DelReturnsTrueThenFalse) {
  KVStore store;
  store.set("foo", "bar");
  EXPECT_TRUE(store.del("foo"));
  EXPECT_FALSE(store.del("foo"));
}

TEST(KVStoreTest, ExistsReflectsCurrentState) {
  KVStore store;
  EXPECT_FALSE(store.exists("foo"));
  store.set("foo", "bar");
  EXPECT_TRUE(store.exists("foo"));
  store.del("foo");
  EXPECT_FALSE(store.exists("foo"));
}

TEST(KVStoreTest, SizeTracksLiveKeys) {
  KVStore store;
  EXPECT_EQ(store.size(), 0u);
  store.set("a", "1");
  store.set("b", "2");
  EXPECT_EQ(store.size(), 2u);
  store.del("a");
  EXPECT_EQ(store.size(), 1u);
}

TEST(KVStoreTest, SetOverwritesExistingValueWithoutGrowingSize) {
  KVStore store;
  store.set("foo", "bar");
  store.set("foo", "baz");
  EXPECT_EQ(*store.get("foo"), "baz");
  EXPECT_EQ(store.size(), 1u);
}

TEST(KVStoreTest, ForEachVisitsEveryEntryExactlyOnce) {
  KVStore store;
  store.set("a", "1");
  store.set("b", "2");
  std::map<std::string, std::string> seen;
  store.forEach([&](const std::string& k, const std::string& v) { seen[k] = v; });
  EXPECT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen["a"], "1");
  EXPECT_EQ(seen["b"], "2");
}

TEST(KVStoreTest, IncrementByOnMissingKeyStartsFromZero) {
  KVStore store;
  EXPECT_EQ(store.incrementBy("counter", 1), 1);
  EXPECT_EQ(store.incrementBy("counter", 1), 2);
  EXPECT_EQ(store.incrementBy("counter", -5), -3);
}

TEST(KVStoreTest, IncrementByOnNonIntegerValueThrows) {
  KVStore store;
  store.set("notanumber", "abc");
  EXPECT_THROW(store.incrementBy("notanumber", 1), std::invalid_argument);
}

TEST(KVStoreTest, IncrementByLeavesTheKeyAsAStringAfterward) {
  KVStore store;
  store.incrementBy("counter", 42);
  EXPECT_EQ(*store.get("counter"), "42");
}
