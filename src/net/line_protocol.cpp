#include "tinykv/net/line_protocol.hpp"

#include <sys/socket.h>

namespace tinykv {

namespace {

// Matches the protocol's max request line length (docs/PROTOCOL.md). A
// client that never sends '\n' would otherwise make buffer_ grow without
// bound; that's a framing-level concern, so it's enforced here rather than
// in Parser, which never even runs until a full line has been buffered.
constexpr size_t kMaxLineLength = 8192;

}  // namespace

bool writeRaw(int fd, const std::string& data) {
  size_t totalSent = 0;
  while (totalSent < data.size()) {
    ssize_t sent = send(fd, data.data() + totalSent, data.size() - totalSent, 0);
    if (sent <= 0) return false;
    totalSent += static_cast<size_t>(sent);
  }
  return true;
}

bool writeLine(int fd, const std::string& line) { return writeRaw(fd, line + "\n"); }

bool LineReader::readLine(int fd, std::string& outLine) {
  while (true) {
    size_t newlinePos = buffer_.find('\n');
    if (newlinePos != std::string::npos) {
      outLine = buffer_.substr(0, newlinePos);
      if (!outLine.empty() && outLine.back() == '\r') outLine.pop_back();
      buffer_.erase(0, newlinePos + 1);
      return true;
    }

    if (buffer_.size() > kMaxLineLength) return false;

    char chunk[4096];
    ssize_t received = recv(fd, chunk, sizeof(chunk), 0);
    if (received <= 0) return false;
    buffer_.append(chunk, static_cast<size_t>(received));
  }
}

}  // namespace tinykv
