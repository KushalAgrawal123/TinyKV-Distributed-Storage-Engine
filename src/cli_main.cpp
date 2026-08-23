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

  std::cout << "==================================\n";
  std::cout << "  TinyKV CLI v0.1 (Phase 1)\n";
  std::cout << "==================================\n";
  std::cout << "Target server port: " << port << "\n";
  std::cout << "(connecting to a server is not implemented yet - see Phase 3)\n";

  return 0;
}
