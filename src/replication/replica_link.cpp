#include "tinykv/replication/replica_link.hpp"

#include <chrono>

#include "tinykv/logger.hpp"

namespace tinykv {

namespace {
constexpr int kReconnectDelaySeconds = 1;
}  // namespace

ReplicaLink::ReplicaLink(std::string primaryHost, int primaryPort, ApplyCallback onLine)
    : primaryHost_(std::move(primaryHost)), primaryPort_(primaryPort), onLine_(std::move(onLine)) {}

ReplicaLink::~ReplicaLink() { stop(); }

void ReplicaLink::start() {
  running_ = true;
  thread_ = std::thread([this] { runLoop(); });
}

void ReplicaLink::stop() {
  running_ = false;
  {
    std::lock_guard<std::mutex> lock(clientMutex_);
    if (currentClient_ != nullptr) {
      currentClient_->close();
    }
  }
  if (thread_.joinable()) thread_.join();
}

void ReplicaLink::runLoop() {
  while (running_) {
    TcpClient client;
    {
      std::lock_guard<std::mutex> lock(clientMutex_);
      currentClient_ = &client;
    }

    if (!client.connect(primaryHost_, primaryPort_)) {
      LOG_WARN("Replica: failed to connect to primary " + primaryHost_ + ":" + std::to_string(primaryPort_));
    } else if (!client.sendLine("SYNC")) {
      LOG_WARN("Replica: failed to send SYNC to primary");
    } else {
      connected_ = true;
      LOG_INFO("Replica: connected to primary " + primaryHost_ + ":" + std::to_string(primaryPort_) +
                ", receiving snapshot + live stream");

      std::string line;
      while (running_ && client.receiveLine(line)) {
        onLine_(line);
      }

      connected_ = false;
      if (running_) {
        LOG_WARN("Replica: lost connection to primary, will reconnect");
      }
    }

    {
      std::lock_guard<std::mutex> lock(clientMutex_);
      currentClient_ = nullptr;
    }

    if (running_) {
      std::this_thread::sleep_for(std::chrono::seconds(kReconnectDelaySeconds));
    }
  }
}

}  // namespace tinykv
