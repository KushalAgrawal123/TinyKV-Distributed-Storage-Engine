#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "tinykv/command_executor.hpp"
#include "tinykv/concurrency/thread_pool.hpp"
#include "tinykv/persistence/aof.hpp"
#include "tinykv/storage/kv_store.hpp"

namespace tinykv {

struct PersistenceConfig {
  bool appendOnly = true;
  std::string dir = "./data";
  std::string dbFilename = "dump.tkv";
  std::string appendFilename = "tinykv.aof";
  int saveIntervalSeconds = 300;  // 0 disables automatic snapshotting
};

// Orchestrates startup recovery (snapshot, then AOF replay) and ongoing
// durability: the live connection handler calls recordWrite() after
// every successfully executed write command, a background timer thread
// flushes the AOF roughly once a second (appendfsync=everysec) and takes
// a fresh snapshot every saveIntervalSeconds, dispatching that actual
// I/O work onto the shared ThreadPool from Phase 5 - its first real
// consumer.
class PersistenceManager {
 public:
  PersistenceManager(PersistenceConfig config, KVStore& store, CommandExecutor& executor, ThreadPool& pool);
  ~PersistenceManager();

  PersistenceManager(const PersistenceManager&) = delete;
  PersistenceManager& operator=(const PersistenceManager&) = delete;

  // Loads the snapshot (if any) then replays the AOF (if any). Call once
  // at startup, before the server starts accepting connections.
  void recover();

  // Call after every successfully executed write command, with the
  // exact request line. No-ops if appendOnly is false.
  void recordWrite(const std::string& requestLine);

  // Immediately snapshots the current state and resets the AOF. Backs
  // the SAVE command; also called periodically by the timer thread.
  void save();

  void start();
  void stop();

 private:
  std::string snapshotPath() const;
  std::string aofPath() const;
  void timerLoop();

  // Runs task on pool_, tracking it so stop() can wait for it to
  // actually finish - a plain ThreadPool::submit() isn't enough here,
  // since a queued task capturing `this` must not still be
  // running/pending when this object gets destroyed.
  void submitBackgroundTask(std::function<void()> task);

  PersistenceConfig config_;
  KVStore& store_;
  CommandExecutor& executor_;
  ThreadPool& pool_;
  AofWriter aofWriter_;
  std::mutex saveMutex_;  // serializes save() against itself (SAVE command vs. the timer)

  std::mutex cvMutex_;
  std::condition_variable cv_;
  bool running_ = false;
  std::thread timerThread_;

  std::atomic<int> inFlightTasks_{0};
  std::mutex tasksDoneMutex_;
  std::condition_variable tasksDoneCv_;
};

}  // namespace tinykv
