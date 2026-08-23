#pragma once

#include <stdexcept>
#include <string>

#include "tinykv/protocol/command.hpp"

namespace tinykv {

// Thrown for a request line that cannot be turned into a Command at all
// (too long, or no command word present). An unrecognized command name is
// NOT a ProtocolError - it becomes CommandType::UNKNOWN so the caller can
// reply with a normal -ERR line instead of tearing down the connection.
class ProtocolError : public std::runtime_error {
 public:
  explicit ProtocolError(const std::string& message) : std::runtime_error(message) {}
};

class Parser {
 public:
  // Parses one wire-protocol request line (see docs/PROTOCOL.md) into a
  // Command. Throws ProtocolError on malformed input.
  static Command parse(const std::string& line);

 private:
  Parser() = default;
};

}  // namespace tinykv
