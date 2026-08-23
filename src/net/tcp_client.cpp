#include "tinykv/net/tcp_client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tinykv {

TcpClient::TcpClient() : fd_(-1) {}

TcpClient::~TcpClient() { close(); }

bool TcpClient::connect(const std::string& host, int port) {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ < 0) return false;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));

  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  return true;
}

void TcpClient::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool TcpClient::sendLine(const std::string& line) {
  if (fd_ < 0) return false;
  return writeLine(fd_, line);
}

bool TcpClient::receiveLine(std::string& outLine) {
  if (fd_ < 0) return false;
  return reader_.readLine(fd_, outLine);
}

bool TcpClient::isConnected() const { return fd_ >= 0; }

}  // namespace tinykv
