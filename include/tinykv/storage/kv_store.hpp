#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace tinykv {

class KVStore {
 public:
  void set(const std::string& key, const std::string& value);
  std::optional<std::string> get(const std::string& key) const;
  bool del(const std::string& key);
  bool exists(const std::string& key) const;
  size_t size() const;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::string> data_;
};

}  // namespace tinykv
