#include <iostream>
#include <string>

#include "tinykv/config.hpp"
#include "tinykv/logger.hpp"
#include "tinykv/storage/kv_store.hpp"

namespace {

struct Args {
  std::string configPath = "tinykv.conf";
  int port = -1;  // -1 means "not set on the command line"
};

Args parseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      args.configPath = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      args.port = std::stoi(argv[++i]);
    }
  }
  return args;
}

int g_passed = 0;
int g_failed = 0;

void check(const std::string& description, bool condition) {
  if (condition) {
    std::cout << "  [PASS] " << description << "\n";
    ++g_passed;
  } else {
    std::cout << "  [FAIL] " << description << "\n";
    ++g_failed;
  }
}

// Temporary hardcoded exercise of KVStore, standing in for real client
// requests until Phase 3 adds TCP networking.
void runStorageSmokeTest() {
  std::cout << "Running KVStore smoke test:\n";
  tinykv::KVStore store;

  check("new store is empty", store.size() == 0);
  check("GET on missing key returns nothing", !store.get("username").has_value());
  check("EXISTS on missing key is false", !store.exists("username"));

  store.set("username", "Kushal");
  check("EXISTS is true after SET username Kushal", store.exists("username"));
  check("GET username returns Kushal", store.get("username") == "Kushal");
  check("size is 1 after one SET", store.size() == 1);

  store.set("username", "Agrawal");
  check("SET on existing key overwrites the value", store.get("username") == "Agrawal");
  check("size stays 1 after overwriting an existing key", store.size() == 1);

  check("DELETE username returns true", store.del("username"));
  check("GET after DELETE returns nothing", !store.get("username").has_value());
  check("size is 0 after DELETE", store.size() == 0);

  check("DELETE on an already-missing key returns false", !store.del("username"));

  std::cout << g_passed << " passed, " << g_failed << " failed\n";
}

}  // namespace

int main(int argc, char** argv) {
  Args args = parseArgs(argc, argv);

  tinykv::Config config;
  config.loadFromFile(args.configPath);

  int port = args.port != -1 ? args.port : config.getInt("port", 6380);

  tinykv::Logger::init(tinykv::LogLevel::INFO);
  LOG_INFO("TinyKV server starting up");
  LOG_INFO("Config file: " + args.configPath);
  LOG_INFO("Port: " + std::to_string(port));

  std::cout << "==================================\n";
  std::cout << "  TinyKV Server v0.2 (Phase 2)\n";
  std::cout << "  Redis-inspired KV store in C++17\n";
  std::cout << "==================================\n";
  std::cout << "Configured port: " << port << "\n";
  std::cout << "(networking not implemented yet - see Phase 3)\n\n";

  runStorageSmokeTest();

  LOG_INFO("TinyKV server shutting down (Phase 2: no server loop yet)");
  return g_failed == 0 ? 0 : 1;
}
