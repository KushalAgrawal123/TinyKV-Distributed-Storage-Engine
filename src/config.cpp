#include "tinykv/config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace tinykv {

namespace {

std::string trim(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

}  // namespace

bool Config::loadFromFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) return false;

  std::string line;
  while (std::getline(file, line)) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;

    size_t eq = trimmed.find('=');
    if (eq == std::string::npos) continue;

    std::string key = trim(trimmed.substr(0, eq));
    std::string value = trim(trimmed.substr(eq + 1));
    if (!key.empty()) {
      values_[key] = value;
    }
  }
  return true;
}

void Config::set(const std::string& key, const std::string& value) { values_[key] = value; }

std::string Config::getString(const std::string& key, const std::string& def) const {
  auto it = values_.find(key);
  return it != values_.end() ? it->second : def;
}

int Config::getInt(const std::string& key, int def) const {
  auto it = values_.find(key);
  if (it == values_.end()) return def;
  try {
    return std::stoi(it->second);
  } catch (...) {
    return def;
  }
}

bool Config::getBool(const std::string& key, bool def) const {
  auto it = values_.find(key);
  if (it == values_.end()) return def;

  std::string v = it->second;
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });

  if (v == "true" || v == "1" || v == "yes") return true;
  if (v == "false" || v == "0" || v == "no") return false;
  return def;
}

}  // namespace tinykv
