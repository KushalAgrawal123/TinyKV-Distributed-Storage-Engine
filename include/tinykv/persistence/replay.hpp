#pragma once

#include <istream>

#include "tinykv/command_executor.hpp"

namespace tinykv {

// Reads '\n'-terminated lines from in and replays each one through
// Parser::parse + CommandExecutor::execute - the same pipeline a live
// client connection uses. Shared by AofLoader and SnapshotManager, since
// both are ultimately "replay a stream of command lines". A line that
// fails to parse is skipped rather than aborting the whole replay: these
// files are only ever written by TinyKV itself, so a malformed line
// signals file corruption or manual editing, not something worth
// crashing startup over.
void replayLines(std::istream& in, CommandExecutor& executor);

}  // namespace tinykv
