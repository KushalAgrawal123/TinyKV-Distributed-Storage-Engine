#include "tinykv/persistence/aof.hpp"

#include "tinykv/persistence/replay.hpp"

namespace tinykv {

AofWriter::AofWriter(const std::string& path) : path_(path) { file_.open(path_, std::ios::app); }

void AofWriter::append(const std::string& line) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_.is_open()) {
    file_ << line << "\n";
  }
}

void AofWriter::flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_.is_open()) {
    file_.flush();
  }
}

void AofWriter::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  file_.close();
  file_.open(path_, std::ios::trunc);
}

bool AofLoader::replay(const std::string& path, CommandExecutor& executor) {
  std::ifstream file(path);
  if (!file.is_open()) return false;
  replayLines(file, executor);
  return true;
}

}  // namespace tinykv
