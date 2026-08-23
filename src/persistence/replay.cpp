#include "tinykv/persistence/replay.hpp"

#include <string>

#include "tinykv/protocol/parser.hpp"

namespace tinykv {

void replayLines(std::istream& in, CommandExecutor& executor) {
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    try {
      Command cmd = Parser::parse(line);
      executor.execute(cmd);
    } catch (const ProtocolError&) {
      // Skip a malformed line rather than aborting the whole replay.
    }
  }
}

}  // namespace tinykv
