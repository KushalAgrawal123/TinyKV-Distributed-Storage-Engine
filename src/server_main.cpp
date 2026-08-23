#include <iostream>
#include <sstream>
#include <string>

#include "tinykv/config.hpp"
#include "tinykv/logger.hpp"
#include "tinykv/net/line_protocol.hpp"
#include "tinykv/net/tcp_server.hpp"
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

// Temporary, ad hoc command dispatch: whitespace-split, straight to
// KVStore. No real parser/validation yet - replaced by the Parser and
// CommandExecutor in Phase 4.
std::string dispatch(tinykv::KVStore& store, const std::string& line) {
  std::istringstream iss(line);
  std::string command;
  iss >> command;

  if (command.empty()) {
    return "";
  }
  if (command == "SET") {
    std::string key, value;
    iss >> key >> value;
    if (key.empty() || value.empty()) return "ERROR wrong number of arguments for SET";
    store.set(key, value);
    return "OK";
  }
  if (command == "GET") {
    std::string key;
    iss >> key;
    if (key.empty()) return "ERROR wrong number of arguments for GET";
    auto value = store.get(key);
    return value.has_value() ? *value : "(nil)";
  }
  if (command == "DEL") {
    std::string key;
    iss >> key;
    if (key.empty()) return "ERROR wrong number of arguments for DEL";
    return store.del(key) ? "1" : "0";
  }
  return "ERROR unknown command '" + command + "'";
}

void handleConnection(tinykv::KVStore& store, int clientFd) {
  tinykv::LineReader reader;
  std::string line;
  while (reader.readLine(clientFd, line)) {
    std::string response = dispatch(store, line);
    if (!response.empty() && !tinykv::writeLine(clientFd, response)) {
      break;
    }
  }
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
  std::cout << "  TinyKV Server v0.3 (Phase 3)\n";
  std::cout << "  Redis-inspired KV store in C++17\n";
  std::cout << "==================================\n";
  std::cout << "Listening on port " << port << "\n";
  std::cout << "Connect with: nc localhost " << port << "   or   tinykv-cli\n";
  std::cout << "(one client at a time for now - see Phase 5 for concurrency)\n\n";

  tinykv::KVStore store;
  tinykv::TcpServer server(port);

  if (!server.start()) {
    LOG_ERROR("Failed to start TCP server on port " + std::to_string(port));
    return 1;
  }

  server.run([&store](int clientFd) { handleConnection(store, clientFd); });

  LOG_INFO("TinyKV server shutting down");
  return 0;
}
