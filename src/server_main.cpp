#include <csignal>
#include <iostream>
#include <sstream>
#include <string>

#include "tinykv/command_executor.hpp"
#include "tinykv/concurrency/thread_pool.hpp"
#include "tinykv/config.hpp"
#include "tinykv/logger.hpp"
#include "tinykv/net/line_protocol.hpp"
#include "tinykv/net/tcp_server.hpp"
#include "tinykv/persistence/persistence_manager.hpp"
#include "tinykv/persistence/snapshot.hpp"
#include "tinykv/protocol/parser.hpp"
#include "tinykv/protocol/reply.hpp"
#include "tinykv/replication/replication_manager.hpp"
#include "tinykv/storage/expiry_manager.hpp"
#include "tinykv/storage/kv_store.hpp"

namespace {

struct Args {
  std::string configPath = "tinykv.conf";
  int port = -1;     // -1 means "not set on the command line"
  int maxKeys = -1;  // -1 means "not set on the command line"
};

Args parseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      args.configPath = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      args.port = std::stoi(argv[++i]);
    } else if (arg == "--max-keys" && i + 1 < argc) {
      args.maxKeys = std::stoi(argv[++i]);
    }
  }
  return args;
}

// Case-sensitive on purpose, matching tinykv.conf's documented values
// exactly (DEBUG/INFO/WARN/ERROR) - an unrecognized value falls back to
// INFO rather than erroring, since a bad log_level shouldn't stop the
// server from starting.
tinykv::LogLevel parseLogLevel(const std::string& level) {
  if (level == "DEBUG") return tinykv::LogLevel::DEBUG;
  if (level == "WARN") return tinykv::LogLevel::WARN;
  if (level == "ERROR") return tinykv::LogLevel::ERROR;
  return tinykv::LogLevel::INFO;
}

// Shared by the live connection handler (a direct client write on a
// primary) and the replication apply callback (a line replayed from
// this server's own primary): if the command actually mutated data,
// record it for durability and forward it to any of THIS server's own
// registered replicas. Applying this uniformly regardless of where the
// write came from is what makes chained replication (a replica with its
// own downstream replicas) work without any extra plumbing.
void recordAndPropagateIfWrite(tinykv::PersistenceManager& persistence, tinykv::ReplicationManager& replication,
                                const tinykv::Command& cmd, const std::string& line, const std::string& response) {
  bool succeeded = !response.empty() && response[0] != '-';
  if (succeeded && tinykv::isWriteCommand(cmd.type)) {
    persistence.recordWrite(line);
    replication.registry().propagate(line);
  }
}

// A connection that sent SYNC stops being a normal request/response
// client and becomes a one-way replica stream: register it, dump the
// current dataset as a burst of SET lines (the same format - and the
// same replay pipeline on the other end - as every future propagated
// write), then just watch for disconnection.
//
// Registering before dumping guarantees no write committed after this
// point is ever missed by the new replica, at the acceptable cost of a
// narrow race: a write landing in the exact instant of the dump could
// both appear in it AND be separately propagated. SET/DEL are idempotent
// under that; INCR/DECR in that same tiny window are not. Fixing that
// fully would mean sharing a lock with KVStore's own internals, which is
// more machinery than this trade-off is worth.
void handleSyncConnection(tinykv::KVStore& store, tinykv::ReplicationManager& replication, int clientFd) {
  replication.registry().add(clientFd);
  LOG_INFO("Replica connected for SYNC (fd " + std::to_string(clientFd) + ", " +
           std::to_string(replication.registry().size()) + " replica(s) now attached)");

  std::ostringstream snapshotBuffer;
  tinykv::SnapshotManager::save(snapshotBuffer, store);
  if (!tinykv::writeRaw(clientFd, snapshotBuffer.str())) {
    replication.registry().remove(clientFd);
    return;
  }

  // A replica doesn't send anything more after SYNC; keep reading (and
  // discarding) just to detect when it disconnects.
  tinykv::LineReader reader;
  std::string line;
  while (reader.readLine(clientFd, line)) {
  }

  replication.registry().remove(clientFd);
  LOG_INFO("Replica disconnected (fd " + std::to_string(clientFd) + ")");
}

void handleConnection(tinykv::KVStore& store, tinykv::CommandExecutor& executor,
                       tinykv::PersistenceManager& persistence, tinykv::ReplicationManager& replication,
                       int clientFd) {
  tinykv::LineReader reader;
  std::string line;
  while (reader.readLine(clientFd, line)) {
    if (line.empty()) {
      continue;
    }

    tinykv::Command cmd;
    try {
      cmd = tinykv::Parser::parse(line);
    } catch (const tinykv::ProtocolError& e) {
      if (!tinykv::writeLine(clientFd, tinykv::Reply::error(e.what()))) break;
      continue;
    }

    if (cmd.type == tinykv::CommandType::SYNC) {
      handleSyncConnection(store, replication, clientFd);
      return;  // this connection is now a replica stream, not a client
    }

    if (tinykv::isWriteCommand(cmd.type) && replication.role() == tinykv::ServerRole::REPLICA) {
      if (!tinykv::writeLine(clientFd, tinykv::Reply::readOnlyError())) break;
      continue;
    }

    std::string response = executor.execute(cmd);
    recordAndPropagateIfWrite(persistence, replication, cmd, line, response);

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
  int maxKeysRaw = args.maxKeys != -1 ? args.maxKeys : config.getInt("max_keys", 0);
  size_t maxKeys = maxKeysRaw > 0 ? static_cast<size_t>(maxKeysRaw) : 0;

  tinykv::PersistenceConfig persistConfig;
  persistConfig.appendOnly = config.getBool("appendonly", true);
  persistConfig.dir = config.getString("dir", "./data");
  persistConfig.dbFilename = config.getString("dbfilename", "dump.tkv");
  persistConfig.appendFilename = config.getString("appendfilename", "tinykv.aof");
  persistConfig.saveIntervalSeconds = config.getInt("save_interval", 300);

  tinykv::Logger::init(parseLogLevel(config.getString("log_level", "INFO")));
  LOG_INFO("TinyKV server starting up");
  LOG_INFO("Config file: " + args.configPath);
  LOG_INFO("Port: " + std::to_string(port));
  LOG_INFO("Max keys (LRU capacity): " + (maxKeys == 0 ? std::string("unbounded") : std::to_string(maxKeys)));
  LOG_INFO("Persistence dir: " + persistConfig.dir + " (appendonly=" +
           (persistConfig.appendOnly ? "yes" : "no") + ")");

  std::cout << "==================================\n";
  std::cout << "  TinyKV Server\n";
  std::cout << "  Redis-inspired KV store in C++17\n";
  std::cout << "==================================\n";
  std::cout << "Listening on port " << port << "\n";
  std::cout << "Connect with: nc localhost " << port << "   or   tinykv-cli\n";
  std::cout << "Commands: SET key value [EX seconds] | GET key | DEL key | PING\n";
  std::cout << "          INCR key | DECR key | TTL key | EXPIRE key seconds | PERSIST key | SAVE\n";
  std::cout << "          REPLICAOF host port | REPLICAOF NO ONE\n";
  std::cout << "(multi-client, thread-safe, LRU-capped, expiring keys, durable, replicated - see docs/PROTOCOL.md)\n\n";

  // Declaration order matters: destruction happens in reverse. store must
  // outlive expiryManager's/persistence's background threads, and both
  // executor and persistence must outlive replication - its destructor
  // stops (and joins) any active ReplicaLink, whose thread calls the
  // apply callback below, which touches executor and persistence.
  tinykv::KVStore store(maxKeys);
  tinykv::ExpiryManager expiryManager;
  tinykv::CommandExecutor executor(store, expiryManager);
  tinykv::ThreadPool backgroundPool(2);
  tinykv::PersistenceManager persistence(persistConfig, store, executor, backgroundPool);
  executor.setPersistence(&persistence);

  tinykv::ReplicationManager replication;
  executor.setReplication(&replication);
  replication.setApplyCallback([&](const std::string& line) {
    try {
      tinykv::Command cmd = tinykv::Parser::parse(line);
      std::string response = executor.execute(cmd);
      recordAndPropagateIfWrite(persistence, replication, cmd, line, response);
    } catch (const tinykv::ProtocolError&) {
      // A malformed line from our own primary shouldn't happen; skip it
      // defensively rather than tearing down the replication link.
    }
  });

  tinykv::TcpServer server(port);

  expiryManager.start([&store](const std::string& key) {
    store.del(key);
    LOG_INFO("Key expired: " + key);
  });

  // Recovery must finish before the server starts accepting connections,
  // so no client (or replica) can observe a partially-restored dataset.
  persistence.recover();

  if (!server.start()) {
    LOG_ERROR("Failed to start TCP server on port " + std::to_string(port));
    return 1;
  }

  persistence.start();

  g_server = &server;
  std::signal(SIGINT, handleShutdownSignal);
  std::signal(SIGTERM, handleShutdownSignal);

  server.run([&](int clientFd) { handleConnection(store, executor, persistence, replication, clientFd); });

  LOG_INFO("TinyKV server shutting down");
  return 0;
}
