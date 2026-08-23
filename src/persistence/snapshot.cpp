#include "tinykv/persistence/snapshot.hpp"

#include <fstream>

#include "tinykv/persistence/replay.hpp"

namespace tinykv {

void SnapshotManager::save(std::ostream& out, KVStore& store) {
  store.forEach([&out](const std::string& key, const std::string& value) {
    out << "SET " << key << " " << value << "\n";
  });
}

void SnapshotManager::load(std::istream& in, CommandExecutor& executor) { replayLines(in, executor); }

bool SnapshotManager::saveToFile(const std::string& path, KVStore& store) {
  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open()) return false;
  save(file, store);
  return true;
}

bool SnapshotManager::loadFromFile(const std::string& path, CommandExecutor& executor) {
  std::ifstream file(path);
  if (!file.is_open()) return false;
  load(file, executor);
  return true;
}

}  // namespace tinykv
