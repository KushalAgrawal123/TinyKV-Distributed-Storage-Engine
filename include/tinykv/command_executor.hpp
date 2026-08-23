#pragma once

#include <string>

#include "tinykv/protocol/command.hpp"
#include "tinykv/storage/kv_store.hpp"

namespace tinykv {

// Single shared entry point for executing an already-parsed Command
// against a KVStore. Always returns a complete, wire-formatted reply line
// (see docs/PROTOCOL.md) and never throws, so callers don't need to wrap
// it in a try/catch the way they do Parser::parse. Meant to be reused
// as-is by AOF replay (Phase 8) and replication apply (Phase 10A), so
// command dispatch logic lives in exactly one place.
class CommandExecutor {
 public:
  explicit CommandExecutor(KVStore& store);

  std::string execute(const Command& cmd);

 private:
  KVStore& store_;
};

}  // namespace tinykv
