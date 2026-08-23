#pragma once

#include <istream>
#include <ostream>
#include <string>

#include "tinykv/command_executor.hpp"
#include "tinykv/storage/kv_store.hpp"

namespace tinykv {

// Serializes/deserializes the full KVStore dataset as a stream of SET
// lines - the same wire format the command layer already speaks, so
// loading a snapshot is just replaying it through the normal
// Parser/CommandExecutor pipeline (see replay.hpp). Deliberately
// stream-based rather than tied to file paths, so Phase 10A's
// replication SYNC can reuse save()/load() to send a full copy of the
// dataset over a socket, not just to/from a file.
class SnapshotManager {
 public:
  static void save(std::ostream& out, KVStore& store);
  static void load(std::istream& in, CommandExecutor& executor);

  // File-path convenience wrappers used by PersistenceManager.
  static bool saveToFile(const std::string& path, KVStore& store);
  static bool loadFromFile(const std::string& path, CommandExecutor& executor);
};

}  // namespace tinykv
