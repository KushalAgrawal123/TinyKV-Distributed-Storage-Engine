#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "tinykv/command_executor.hpp"
#include "tinykv/concurrency/thread_pool.hpp"
#include "tinykv/protocol/parser.hpp"
#include "tinykv/storage/expiry_manager.hpp"
#include "tinykv/storage/kv_store.hpp"

using namespace tinykv;

TEST(ConcurrencyTest, ConcurrentIncrementByNeverLosesAnUpdate) {
  KVStore store;
  constexpr int kThreads = 8;
  constexpr int kIncrementsPerThread = 2000;

  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&store] {
      for (int j = 0; j < kIncrementsPerThread; ++j) {
        store.incrementBy("counter", 1);
      }
    });
  }
  for (auto& t : threads) t.join();

  auto value = store.get("counter");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, std::to_string(kThreads * kIncrementsPerThread));
}

TEST(ConcurrencyTest, ConcurrentSetsOnDistinctKeysAllSurvive) {
  KVStore store;
  constexpr int kThreads = 8;
  constexpr int kKeysPerThread = 200;

  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&store, t] {
      for (int i = 0; i < kKeysPerThread; ++i) {
        store.set("t" + std::to_string(t) + "_k" + std::to_string(i), "v");
      }
    });
  }
  for (auto& th : threads) th.join();

  EXPECT_EQ(store.size(), static_cast<size_t>(kThreads * kKeysPerThread));
}

// The same race the Phase 6 smoke test checks over a real socket
// (concurrent SETs on one shared key never crash the server and every
// GET sees a fully-written value), exercised directly against
// CommandExecutor so it also runs under sanitizers/CI without sockets.
TEST(ConcurrencyTest, ConcurrentCommandExecutorNeverProducesAGarbledReply) {
  KVStore store;
  ExpiryManager expiry;
  CommandExecutor executor(store, expiry);
  constexpr int kThreads = 6;
  constexpr int kOpsPerThread = 500;

  std::vector<std::thread> threads;
  std::atomic<int> failures{0};
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      Command setCmd = Parser::parse("SET shared t" + std::to_string(t));
      Command getCmd = Parser::parse("GET shared");
      for (int i = 0; i < kOpsPerThread; ++i) {
        if (executor.execute(setCmd) != "+OK") ++failures;
        std::string got = executor.execute(getCmd);
        // A different thread may have overwritten "shared" since - this
        // only checks the reply is always a complete, well-formed value,
        // never a torn/partial one.
        if (got.empty() || got[0] != '+') ++failures;
      }
    });
  }
  for (auto& th : threads) th.join();
  EXPECT_EQ(failures.load(), 0);
}

TEST(ConcurrencyTest, ManyConcurrentReadersAllSeeTheSameWrittenValue) {
  KVStore store;
  store.set("key", "value");
  constexpr int kReaders = 16;
  constexpr int kReadsPerThread = 1000;
  std::atomic<int> successfulReads{0};

  std::vector<std::thread> threads;
  for (int i = 0; i < kReaders; ++i) {
    threads.emplace_back([&] {
      for (int j = 0; j < kReadsPerThread; ++j) {
        auto v = store.get("key");
        if (v.has_value() && *v == "value") ++successfulReads;
      }
    });
  }
  for (auto& t : threads) t.join();
  EXPECT_EQ(successfulReads.load(), kReaders * kReadsPerThread);
}

TEST(ConcurrencyTest, ThreadPoolRunsEveryTaskExactlyOnce) {
  ThreadPool pool(4);
  constexpr int kTasks = 500;
  std::atomic<int> completed{0};
  for (int i = 0; i < kTasks; ++i) {
    pool.submit([&completed] { ++completed; });
  }

  // ThreadPool has no "wait for all submitted tasks" API by design (see
  // its header) - poll with a bounded timeout instead. This only checks
  // that every task eventually completes exactly once, not ordering.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (completed.load() < kTasks && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_EQ(completed.load(), kTasks);
}

TEST(ConcurrencyTest, ThreadPoolTasksSubmittedFromMultipleThreadsAllRun) {
  ThreadPool pool(4);
  constexpr int kSubmitters = 4;
  constexpr int kTasksPerSubmitter = 200;
  std::atomic<int> completed{0};

  std::vector<std::thread> submitters;
  for (int s = 0; s < kSubmitters; ++s) {
    submitters.emplace_back([&pool, &completed] {
      for (int i = 0; i < kTasksPerSubmitter; ++i) {
        pool.submit([&completed] { ++completed; });
      }
    });
  }
  for (auto& t : submitters) t.join();

  constexpr int kExpected = kSubmitters * kTasksPerSubmitter;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (completed.load() < kExpected && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_EQ(completed.load(), kExpected);
}
