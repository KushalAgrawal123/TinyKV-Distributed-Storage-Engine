#pragma once

#include <string>
#include <unordered_map>

namespace tinykv {

class Config {
 public:
  bool loadFromFile(const std::string& path);

  void set(const std::string& key, const std::string& value);

  std::string getString(const std::string& key, const std::string& def = "") const;
  int getInt(const std::string& key, int def = 0) const;
  bool getBool(const std::string& key, bool def = false) const;

 private:
  std::unordered_map<std::string, std::string> values_;
};

}  // namespace tinykv
