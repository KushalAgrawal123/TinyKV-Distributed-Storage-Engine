#include <iostream>
#include <string>

#include "tinykv/config.hpp"
#include "tinykv/logger.hpp"

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
  std::cout << "  TinyKV Server v0.1 (Phase 1)\n";
  std::cout << "  Redis-inspired KV store in C++17\n";
  std::cout << "==================================\n";
  std::cout << "Configured port: " << port << "\n";
  std::cout << "(networking not implemented yet - see Phase 3)\n";

  LOG_INFO("TinyKV server shutting down (Phase 1: no server loop yet)");
  return 0;
}
