#pragma once

#include <atomic>
#include <functional>

namespace tinykv {

class TcpServer {
 public:
  explicit TcpServer(int port);
  ~TcpServer();

  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  // Creates, binds, and listens on the socket. Returns false on failure.
  bool start();

  // Blocking accept loop: each accepted connection is handed to its own
  // detached thread running connectionHandler, so multiple clients are
  // served concurrently. Returns once stop() has been called (from
  // another thread, e.g. a signal handler) or accept() hits a fatal
  // error - after giving any still-running connection threads a bounded
  // window to finish.
  void run(const std::function<void(int clientFd)>& connectionHandler);

  // Signals the accept loop to exit and unblocks it by closing the
  // listening socket. Deliberately minimal (flag flip + close()) so it's
  // safe to call directly from a signal handler.
  void stop();

 private:
  int port_;
  int listenFd_;
  std::atomic<bool> running_;
  std::atomic<int> activeConnections_;
};

}  // namespace tinykv
