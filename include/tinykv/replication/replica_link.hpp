#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "tinykv/net/tcp_client.hpp"

namespace tinykv {

// Replica-side connection to a primary: connects, sends SYNC, then reads
// a stream of command lines and hands each to onLine - no distinction is
// made between "the initial full dump" and "an ongoing live update":
// both are just SET/DEL/etc. lines, and replaying either through
// CommandExecutor::execute() the normal way reconstructs the same state.
// Reconnects with a fixed backoff if the primary drops or was never
// reachable.
class ReplicaLink {
 public:
  using ApplyCallback = std::function<void(const std::string& line)>;

  ReplicaLink(std::string primaryHost, int primaryPort, ApplyCallback onLine);
  ~ReplicaLink();

  ReplicaLink(const ReplicaLink&) = delete;
  ReplicaLink& operator=(const ReplicaLink&) = delete;

  void start();
  void stop();

  bool isConnected() const { return connected_.load(); }

 private:
  void runLoop();

  std::string primaryHost_;
  int primaryPort_;
  ApplyCallback onLine_;

  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};
  std::thread thread_;

  // Points at the current attempt's TcpClient while connected, so stop()
  // can close() it to unblock a pending connect()/receiveLine() the same
  // way TcpServer::stop() closes the listening socket to unblock accept().
  std::mutex clientMutex_;
  TcpClient* currentClient_ = nullptr;
};

}  // namespace tinykv
