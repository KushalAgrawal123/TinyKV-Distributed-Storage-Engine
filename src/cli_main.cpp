#include <iostream>
#include <string>

#include "tinykv/config.hpp"
#include "tinykv/logger.hpp"
#include "tinykv/net/tcp_client.hpp"

namespace {

struct Args {
  std::string configPath = "tinykv.conf";
  std::string host = "127.0.0.1";
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
    } else if (arg == "--host" && i + 1 < argc) {
      args.host = argv[++i];
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
  std::cout << "  TinyKV CLI v0.3 (Phase 3)\n";
  std::cout << "==================================\n";

  tinykv::TcpClient client;
  if (!client.connect(args.host, port)) {
    std::cerr << "Failed to connect to " << args.host << ":" << port << "\n";
    return 1;
  }
  std::cout << "Connected to " << args.host << ":" << port << "\n";
  std::cout << "Type commands (SET key value / GET key / DEL key), or 'quit' to exit.\n";

  std::string input;
  while (true) {
    std::cout << "tinykv> ";
    if (!std::getline(std::cin, input)) {
      break;  // EOF (Ctrl+D)
    }
    if (input == "quit" || input == "exit") {
      break;
    }
    if (input.empty()) {
      continue;
    }

    if (!client.sendLine(input)) {
      std::cerr << "Connection lost while sending.\n";
      break;
    }

    std::string response;
    if (!client.receiveLine(response)) {
      std::cerr << "Connection lost while waiting for a reply.\n";
      break;
    }
    std::cout << response << "\n";
  }

  std::cout << "Bye.\n";
  return 0;
}
