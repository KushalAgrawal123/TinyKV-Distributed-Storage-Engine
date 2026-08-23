#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "tinykv/protocol/command.hpp"
#include "tinykv/storage/expiry_manager.hpp"
#include "tinykv/storage/kv_store.hpp"

namespace tinykv {

// Forward-declared, not included: PersistenceManager needs CommandExecutor
// by reference (for AOF/snapshot replay), so a full #include here would
// be circular. CommandExecutor only ever touches it through a pointer
// (for the SAVE command), which a forward declaration is enough for; the
// full type is included in command_executor.cpp where it's actually used.
class PersistenceManager;

// Single shared entry point for executing an already-parsed Command
// against a KVStore (and its associated ExpiryManager). Always returns a
// complete, wire-formatted reply line (see docs/PROTOCOL.md) and never
// throws, so callers don't need to wrap it in a try/catch the way they do
// Parser::parse. Meant to be reused as-is by AOF replay and snapshot
// loading (Phase 8) and replication apply (Phase 10A), so command
// dispatch logic lives in exactly one place.
class CommandExecutor {
 public:
  CommandExecutor(KVStore& store, ExpiryManager& expiry);

  // Wires up the SAVE command; optional, and set after construction to
  // break the CommandExecutor <-> PersistenceManager construction-order
  // cycle (PersistenceManager's constructor needs a CommandExecutor to
  // already exist).
  void setPersistence(PersistenceManager* persistence);

  std::string execute(const Command& cmd);

  // Telemetry, safe to read from any thread while execute() runs
  // concurrently on others.
  uint64_t totalCommands() const { return totalCommands_.load(); }
  uint64_t totalHits() const { return totalHits_.load(); }
  uint64_t totalMisses() const { return totalMisses_.load(); }

 private:
  KVStore& store_;
  ExpiryManager& expiry_;
  PersistenceManager* persistence_ = nullptr;
  std::atomic<uint64_t> totalCommands_{0};
  std::atomic<uint64_t> totalHits_{0};
  std::atomic<uint64_t> totalMisses_{0};
};

}  // namespace tinykv
