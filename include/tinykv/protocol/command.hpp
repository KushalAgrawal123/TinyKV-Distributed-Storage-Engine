#pragma once

#include <string>
#include <vector>

namespace tinykv {

enum class CommandType { SET, GET, DEL, PING, INCR, DECR, TTL, EXPIRE, PERSIST, SAVE, REPLICAOF, SYNC, ROUTE, UNKNOWN };

struct Command {
  CommandType type = CommandType::UNKNOWN;
  std::string name;               // command word exactly as received, for error messages
  std::vector<std::string> args;  // everything after the command word
};

// True for commands that mutate stored data (or its expiry) and should
// therefore be appended to the AOF after a successful execute() - see
// PersistenceManager::recordWrite. SAVE itself isn't a write command by
// this definition: it doesn't change what's stored, only when it's
// durably captured, so it's never recorded to (or replayed from) the
// AOF/snapshot it produces.
inline bool isWriteCommand(CommandType type) {
  switch (type) {
    case CommandType::SET:
    case CommandType::DEL:
    case CommandType::INCR:
    case CommandType::DECR:
    case CommandType::EXPIRE:
    case CommandType::PERSIST:
      return true;
    default:
      return false;
  }
}

}  // namespace tinykv
