#pragma once

#include <string>

#include "tinykv/net/line_protocol.hpp"

namespace tinykv {

class TcpClient {
 public:
  TcpClient();
  ~TcpClient();

  TcpClient(const TcpClient&) = delete;
  TcpClient& operator=(const TcpClient&) = delete;

  bool connect(const std::string& host, int port);
  void close();

  bool sendLine(const std::string& line);
  // Blocks until a full line is received. Returns false on disconnect/error.
  bool receiveLine(std::string& outLine);

  bool isConnected() const;

 private:
  int fd_;
  LineReader reader_;
};

}  // namespace tinykv
