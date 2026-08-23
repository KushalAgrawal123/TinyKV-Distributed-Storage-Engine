#include "tinykv/net/tcp_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "tinykv/logger.hpp"

namespace tinykv {

TcpServer::TcpServer(int port) : port_(port), listenFd_(-1), running_(false) {}

TcpServer::~TcpServer() { stop(); }

bool TcpServer::start() {
  listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd_ < 0) {
    LOG_ERROR(std::string("socket() failed: ") + std::strerror(errno));
    return false;
  }

  int opt = 1;
  if (setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    LOG_ERROR(std::string("setsockopt(SO_REUSEADDR) failed: ") + std::strerror(errno));
    ::close(listenFd_);
    listenFd_ = -1;
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(static_cast<uint16_t>(port_));

  if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    LOG_ERROR("bind() to port " + std::to_string(port_) + " failed: " + std::strerror(errno));
    ::close(listenFd_);
    listenFd_ = -1;
    return false;
  }

  const int backlog = 128;
  if (listen(listenFd_, backlog) < 0) {
    LOG_ERROR(std::string("listen() failed: ") + std::strerror(errno));
    ::close(listenFd_);
    listenFd_ = -1;
    return false;
  }

  running_ = true;
  LOG_INFO("TcpServer listening on port " + std::to_string(port_));
  return true;
}

void TcpServer::run(const std::function<void(int)>& connectionHandler) {
  while (running_) {
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

    if (clientFd < 0) {
      if (running_) {
        LOG_WARN(std::string("accept() failed: ") + std::strerror(errno));
      }
      continue;
    }

    int one = 1;
    setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    char ipStr[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
    LOG_INFO(std::string("Client connected: ") + ipStr + ":" + std::to_string(ntohs(clientAddr.sin_port)));

    connectionHandler(clientFd);

    ::close(clientFd);
    LOG_INFO("Client disconnected");
  }
}

void TcpServer::stop() {
  running_ = false;
  if (listenFd_ >= 0) {
    ::close(listenFd_);
    listenFd_ = -1;
  }
}

}  // namespace tinykv
