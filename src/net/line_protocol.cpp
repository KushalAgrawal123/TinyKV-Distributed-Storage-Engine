#include "tinykv/net/line_protocol.hpp"

#include <sys/socket.h>

namespace tinykv {

bool writeLine(int fd, const std::string& line) {
  std::string withTerminator = line + "\n";
  size_t totalSent = 0;
  while (totalSent < withTerminator.size()) {
    ssize_t sent = send(fd, withTerminator.data() + totalSent, withTerminator.size() - totalSent, 0);
    if (sent <= 0) return false;
    totalSent += static_cast<size_t>(sent);
  }
  return true;
}

bool LineReader::readLine(int fd, std::string& outLine) {
  while (true) {
    size_t newlinePos = buffer_.find('\n');
    if (newlinePos != std::string::npos) {
      outLine = buffer_.substr(0, newlinePos);
      if (!outLine.empty() && outLine.back() == '\r') outLine.pop_back();
      buffer_.erase(0, newlinePos + 1);
      return true;
    }

    char chunk[4096];
    ssize_t received = recv(fd, chunk, sizeof(chunk), 0);
    if (received <= 0) return false;
    buffer_.append(chunk, static_cast<size_t>(received));
  }
}

}  // namespace tinykv
