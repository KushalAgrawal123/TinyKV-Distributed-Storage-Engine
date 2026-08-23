#include <csignal>
#include <iostream>
#include <string>

#include "tinykv/command_executor.hpp"
#include "tinykv/config.hpp"
#include "tinykv/logger.hpp"
#include "tinykv/net/line_protocol.hpp"
#include "tinykv/net/tcp_server.hpp"
#include "tinykv/protocol/parser.hpp"
#include "tinykv/protocol/reply.hpp"
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

void handleConnection(tinykv::CommandExecutor& executor, int clientFd) {
  tinykv::LineReader reader;
  std::string line;
  while (reader.readLine(clientFd, line)) {
    if (line.empty()) {
      continue;
    }

    std::string response;
    try {
      tinykv::Command cmd = tinykv::Parser::parse(line);
      response = executor.execute(cmd);
    } catch (const tinykv::ProtocolError& e) {
      response = tinykv::Reply::error(e.what());
    }

    if (!tinykv::writeLine(clientFd, response)) {
      break;
    }
  }
}

// Set once the server is started; a signal handler can only reach it
// through global state. TcpServer::stop() is deliberately minimal (a flag
// flip and a close()) so it's safe to call directly here.
tinykv::TcpServer* g_server = nullptr;

void handleShutdownSignal(int /*signal*/) {
  if (g_server != nullptr) {
    g_server->stop();
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
  std::cout << "  TinyKV Server v0.5 (Phase 5)\n";
  std::cout << "  Redis-inspired KV store in C++17\n";
  std::cout << "==================================\n";
  std::cout << "Listening on port " << port << "\n";
  std::cout << "Connect with: nc localhost " << port << "   or   tinykv-cli\n";
  std::cout << "Commands: SET key value | GET key | DEL key | PING\n";
  std::cout << "(now serving multiple clients concurrently)\n\n";

  tinykv::KVStore store;
  tinykv::CommandExecutor executor(store);
  tinykv::TcpServer server(port);

  if (!server.start()) {
    LOG_ERROR("Failed to start TCP server on port " + std::to_string(port));
    return 1;
  }

  g_server = &server;
  std::signal(SIGINT, handleShutdownSignal);
  std::signal(SIGTERM, handleShutdownSignal);

  server.run([&executor](int clientFd) { handleConnection(executor, clientFd); });

  LOG_INFO("TinyKV server shutting down");
  return 0;
}
