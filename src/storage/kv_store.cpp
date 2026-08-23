#include "tinykv/storage/kv_store.hpp"

#include <stdexcept>

namespace tinykv {

void KVStore::set(const std::string& key, const std::string& value) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  data_[key] = value;
}

std::optional<std::string> KVStore::get(const std::string& key) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = data_.find(key);
  if (it == data_.end()) return std::nullopt;
  return it->second;
}

bool KVStore::del(const std::string& key) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  return data_.erase(key) > 0;
}

bool KVStore::exists(const std::string& key) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return data_.find(key) != data_.end();
}

size_t KVStore::size() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return data_.size();
}

long long KVStore::incrementBy(const std::string& key, long long delta) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  long long current = 0;
  auto it = data_.find(key);
  if (it != data_.end()) {
    try {
      current = std::stoll(it->second);
    } catch (...) {
      throw std::invalid_argument("value is not an integer");
    }
  }

  long long updated = current + delta;
  data_[key] = std::to_string(updated);
  return updated;
}

}  // namespace tinykv
