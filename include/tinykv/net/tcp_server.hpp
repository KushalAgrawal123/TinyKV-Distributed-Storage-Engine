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

  // Blocking accept loop: one connection is fully handled by
  // connectionHandler before the next is accepted. Returns once stop() has
  // been called (from another thread) or accept() hits a fatal error.
  void run(const std::function<void(int clientFd)>& connectionHandler);

  void stop();

 private:
  int port_;
  int listenFd_;
  std::atomic<bool> running_;
};

}  // namespace tinykv
