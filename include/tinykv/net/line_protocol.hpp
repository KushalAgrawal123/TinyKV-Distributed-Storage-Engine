#pragma once

#include <string>

namespace tinykv {

// Writes `line` followed by the protocol's '\n' terminator to fd, retrying
// on partial writes. Returns false on error.
bool writeLine(int fd, const std::string& line);

// Buffers bytes read from a TCP socket and splits them into '\n'-terminated
// lines (a trailing '\r' is stripped). A single recv() can return a partial
// line, multiple lines, or both, so the read buffer must persist across
// calls for the same connection - hence a class rather than a free function.
class LineReader {
 public:
  // Blocks until a full line is available on fd. Returns false on
  // disconnect/error.
  bool readLine(int fd, std::string& outLine);

 private:
  std::string buffer_;
};

}  // namespace tinykv
