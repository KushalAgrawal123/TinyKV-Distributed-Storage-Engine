#include <csignal>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "tinykv/config.hpp"
#include "tinykv/logger.hpp"
#include "tinykv/net/tcp_server.hpp"
#include "tinykv/sharding/router.hpp"

namespace {

struct Args {
  std::string configPath = "router.conf";
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

// router.conf's "backends" is one comma-separated line (e.g.
// "127.0.0.1:6381,127.0.0.1:6382,127.0.0.1:6383") rather than one config
// key per backend, since Config only supports flat key=value pairs and
// the shard list is naturally variable-length.
std::vector<std::string> parseBackends(const std::string& csv) {
  std::vector<std::string> backends;
  std::istringstream iss(csv);
  std::string token;
  while (std::getline(iss, token, ',')) {
    if (!token.empty()) backends.push_back(token);
  }
  return backends;
}

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

  int port = args.port != -1 ? args.port : config.getInt("port", 7000);
  std::vector<std::string> backends = parseBackends(config.getString("backends", ""));

  tinykv::Logger::init(tinykv::LogLevel::INFO);
  LOG_INFO("TinyKV router starting up");
  LOG_INFO("Config file: " + args.configPath);
  LOG_INFO("Port: " + std::to_string(port));

  if (backends.empty()) {
    LOG_ERROR("No backends configured - set 'backends' in " + args.configPath);
    return 1;
  }

  std::ostringstream backendList;
  for (size_t i = 0; i < backends.size(); ++i) {
    if (i > 0) backendList << ", ";
    backendList << backends[i];
  }
  LOG_INFO("Backends: " + backendList.str());

  std::cout << "==================================\n";
  std::cout << "  TinyKV Router v0.10b (Phase 10B)\n";
  std::cout << "==================================\n";
  std::cout << "Listening on port " << port << "\n";
  std::cout << "Routing across " << backends.size() << " shard(s): " << backendList.str() << "\n";
  std::cout << "Connect with: nc localhost " << port << "   or   tinykv-cli\n";
  std::cout << "All normal commands are transparently forwarded by key; ROUTE key shows the target shard.\n\n";

  tinykv::Router router(backends);
  tinykv::TcpServer server(port);

  g_server = &server;
  std::signal(SIGINT, handleShutdownSignal);
  std::signal(SIGTERM, handleShutdownSignal);

  if (!server.start()) {
    LOG_ERROR("Failed to start TCP server on port " + std::to_string(port));
    return 1;
  }

  server.run([&](int clientFd) { router.handleConnection(clientFd); });

  LOG_INFO("TinyKV router shutting down");
  return 0;
}
