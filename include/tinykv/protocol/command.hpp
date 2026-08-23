#pragma once

#include <string>
#include <vector>

namespace tinykv {

enum class CommandType { SET, GET, DEL, PING, UNKNOWN };

struct Command {
  CommandType type = CommandType::UNKNOWN;
  std::string name;              // command word exactly as received, for error messages
  std::vector<std::string> args;  // everything after the command word
};

}  // namespace tinykv
