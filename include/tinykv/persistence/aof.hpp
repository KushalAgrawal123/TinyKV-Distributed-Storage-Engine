#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace tinykv {

class CommandExecutor;  // see the forward-declaration note in command_executor.hpp

// Appends write-command lines to the append-only file as they happen.
// Not flushed after every append (that would defeat the point of
// appendfsync=everysec) - see PersistenceManager for the flush cadence.
class AofWriter {
 public:
  explicit AofWriter(const std::string& path);

  // Appends line + '\n'. Thread-safe: multiple client-handling threads
  // can call this concurrently.
  void append(const std::string& line);

  void flush();

  // Truncates the file back to empty and reopens it - used once SAVE has
  // captured the current state in a fresh snapshot, so replaying the
  // (now-empty) AOF on top of that snapshot is a no-op.
  void reset();

 private:
  std::string path_;
  std::mutex mutex_;
  std::ofstream file_;
};

class AofLoader {
 public:
  // Replays path's contents through executor. Returns false if the file
  // doesn't exist yet - that's not an error, just nothing to replay.
  static bool replay(const std::string& path, CommandExecutor& executor);
};

}  // namespace tinykv
