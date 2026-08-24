#include <csignal>
#include <iostream>
#include <string>

#include "tinykv/config.hpp"
#include "tinykv/logger.hpp"
#include "tinykv/net/tcp_server.hpp"
#include "tinykv/sharding/health_checker.hpp"
#include "tinykv/sharding/router.hpp"
#include "tinykv/sharding/shard_topology.hpp"

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
  std::string topologyPath = config.getString("topology_file", "topology.conf");
  int healthCheckIntervalMs = config.getInt("health_check_interval_ms", 1000);
  int maxMissedPings = config.getInt("max_missed_pings", 3);

  tinykv::Config topologyConfig;
  if (!topologyConfig.loadFromFile(topologyPath)) {
    std::cerr << "Failed to read topology file: " << topologyPath << "\n";
    return 1;
  }
  tinykv::ShardTopology topology(tinykv::ShardTopology::parseShards(topologyConfig));

  tinykv::Logger::init(tinykv::LogLevel::INFO);
  LOG_INFO("TinyKV router starting up");
  LOG_INFO("Config file: " + args.configPath);
  LOG_INFO("Topology file: " + topologyPath);
  LOG_INFO("Port: " + std::to_string(port));
  LOG_INFO("Health check: every " + std::to_string(healthCheckIntervalMs) + "ms, DOWN after " +
           std::to_string(maxMissedPings) + " missed pings");

  auto shardIds = topology.shardIds();
  if (shardIds.empty()) {
    LOG_ERROR("No shards declared - set 'shards' in " + topologyPath);
    return 1;
  }
  LOG_INFO(std::to_string(shardIds.size()) + " shard(s) configured");

  std::cout << "==================================\n";
  std::cout << "  TinyKV Router v0.10c (Phase 10C)\n";
  std::cout << "==================================\n";
  std::cout << "Listening on port " << port << "\n";
  std::cout << "Shards: " << shardIds.size() << " (topology: " << topologyPath << ")\n";
  std::cout << "Connect with: nc localhost " << port << "   or   tinykv-cli\n";
  std::cout << "All normal commands are transparently forwarded by key; ROUTE key shows the target node,\n";
  std::cout << "NODES lists every shard's primary/replica with health and active status.\n\n";

  tinykv::Router router(topology);
  tinykv::HealthChecker healthChecker(topology, healthCheckIntervalMs, maxMissedPings);
  tinykv::TcpServer server(port);

  g_server = &server;
  std::signal(SIGINT, handleShutdownSignal);
  std::signal(SIGTERM, handleShutdownSignal);

  if (!server.start()) {
    LOG_ERROR("Failed to start TCP server on port " + std::to_string(port));
    return 1;
  }

  healthChecker.start();
  server.run([&](int clientFd) { router.handleConnection(clientFd); });
  healthChecker.stop();

  LOG_INFO("TinyKV router shutting down");
  return 0;
}
