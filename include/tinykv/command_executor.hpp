#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "tinykv/protocol/command.hpp"
#include "tinykv/storage/expiry_manager.hpp"
#include "tinykv/storage/kv_store.hpp"

namespace tinykv {

// Single shared entry point for executing an already-parsed Command
// against a KVStore (and its associated ExpiryManager). Always returns a
// complete, wire-formatted reply line (see docs/PROTOCOL.md) and never
// throws, so callers don't need to wrap it in a try/catch the way they do
// Parser::parse. Meant to be reused as-is by AOF replay (Phase 8) and
// replication apply (Phase 10A), so command dispatch logic lives in
// exactly one place.
class CommandExecutor {
 public:
  CommandExecutor(KVStore& store, ExpiryManager& expiry);

  std::string execute(const Command& cmd);

  // Telemetry, safe to read from any thread while execute() runs
  // concurrently on others.
  uint64_t totalCommands() const { return totalCommands_.load(); }
  uint64_t totalHits() const { return totalHits_.load(); }
  uint64_t totalMisses() const { return totalMisses_.load(); }

 private:
  KVStore& store_;
  ExpiryManager& expiry_;
  std::atomic<uint64_t> totalCommands_{0};
  std::atomic<uint64_t> totalHits_{0};
  std::atomic<uint64_t> totalMisses_{0};
};

}  // namespace tinykv
