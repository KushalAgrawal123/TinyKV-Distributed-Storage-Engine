#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace tinykv {

// A fixed-size pool of worker threads pulling tasks off a shared queue.
// Reserved for background jobs (the expiration sweeper in Phase 7, the
// periodic snapshotter in Phase 8) - client connection I/O uses its own
// thread-per-connection model (see TcpServer) instead, so each threading
// concept in the project has one honest, distinct job.
class ThreadPool {
 public:
  explicit ThreadPool(size_t numThreads);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  void submit(std::function<void()> task);

 private:
  void workerLoop();

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex queueMutex_;
  std::condition_variable cv_;
  bool stop_ = false;
};

}  // namespace tinykv
