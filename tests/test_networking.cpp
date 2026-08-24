#include <gtest/gtest.h>

#include <thread>

#include "tinykv/command_executor.hpp"
#include "tinykv/net/line_protocol.hpp"
#include "tinykv/net/tcp_client.hpp"
#include "tinykv/net/tcp_server.hpp"
#include "tinykv/protocol/parser.hpp"
#include "tinykv/protocol/reply.hpp"
#include "tinykv/storage/expiry_manager.hpp"
#include "tinykv/storage/kv_store.hpp"

namespace {

using namespace tinykv;

// A minimal end-to-end server: real TcpServer socket I/O wired straight
// to Parser/CommandExecutor - the same pipeline server_main.cpp uses,
// without pulling in persistence/replication. Each test gets its own
// instance on its own port so tests can run without interfering with
// each other.
class TestServer {
 public:
  explicit TestServer(int port) : server_(port), executor_(store_, expiry_) {}

  ~TestServer() { stop(); }

  bool start() {
    if (!server_.start()) return false;
    thread_ = std::thread([this] { server_.run([this](int fd) { handle(fd); }); });
    return true;
  }

  void stop() {
    server_.stop();
    if (thread_.joinable()) thread_.join();
  }

 private:
  void handle(int fd) {
    LineReader reader;
    std::string line;
    while (reader.readLine(fd, line)) {
      if (line.empty()) continue;
      Command cmd;
      try {
        cmd = Parser::parse(line);
      } catch (const ProtocolError& e) {
        if (!writeLine(fd, Reply::error(e.what()))) break;
        continue;
      }
      if (!writeLine(fd, executor_.execute(cmd))) break;
    }
  }

  TcpServer server_;
  KVStore store_;
  ExpiryManager expiry_;
  CommandExecutor executor_;
  std::thread thread_;
};

// Distinct, high, fixed ports per test - safe because ctest (per this
// project's own verification step) runs tests serially by default, and
// each TestServer is fully stopped/joined before its test returns.
constexpr int kBasePort = 19500;

TEST(NetworkingTest, SetThenGetOverARealSocket) {
  TestServer server(kBasePort + 0);
  ASSERT_TRUE(server.start());

  TcpClient client;
  ASSERT_TRUE(client.connect("127.0.0.1", kBasePort + 0));

  ASSERT_TRUE(client.sendLine("SET foo bar"));
  std::string reply;
  ASSERT_TRUE(client.receiveLine(reply));
  EXPECT_EQ(reply, "+OK");

  ASSERT_TRUE(client.sendLine("GET foo"));
  ASSERT_TRUE(client.receiveLine(reply));
  EXPECT_EQ(reply, "+bar");

  client.close();
}

TEST(NetworkingTest, GetOnMissingKeyIsNilOverTheWire) {
  TestServer server(kBasePort + 1);
  ASSERT_TRUE(server.start());

  TcpClient client;
  ASSERT_TRUE(client.connect("127.0.0.1", kBasePort + 1));
  ASSERT_TRUE(client.sendLine("GET nope"));
  std::string reply;
  ASSERT_TRUE(client.receiveLine(reply));
  EXPECT_EQ(reply, "$-1");

  client.close();
}

TEST(NetworkingTest, MalformedLineGetsACleanErrorNotADisconnect) {
  TestServer server(kBasePort + 2);
  ASSERT_TRUE(server.start());

  TcpClient client;
  ASSERT_TRUE(client.connect("127.0.0.1", kBasePort + 2));

  ASSERT_TRUE(client.sendLine("BOGUS"));
  std::string reply;
  ASSERT_TRUE(client.receiveLine(reply));
  EXPECT_EQ(reply, "-ERR unknown command 'BOGUS'");

  // The connection must still be usable after a bad command.
  ASSERT_TRUE(client.sendLine("PING"));
  ASSERT_TRUE(client.receiveLine(reply));
  EXPECT_EQ(reply, "+OK");

  client.close();
}

TEST(NetworkingTest, ServerServesMultipleSequentialClients) {
  TestServer server(kBasePort + 3);
  ASSERT_TRUE(server.start());

  for (int i = 0; i < 5; ++i) {
    TcpClient client;
    ASSERT_TRUE(client.connect("127.0.0.1", kBasePort + 3));
    ASSERT_TRUE(client.sendLine("SET seq" + std::to_string(i) + " " + std::to_string(i)));
    std::string reply;
    ASSERT_TRUE(client.receiveLine(reply));
    EXPECT_EQ(reply, "+OK");
    client.close();
  }
}

TEST(NetworkingTest, ServerAcceptsANewClientAfterAnotherDisconnects) {
  TestServer server(kBasePort + 4);
  ASSERT_TRUE(server.start());

  {
    TcpClient first;
    ASSERT_TRUE(first.connect("127.0.0.1", kBasePort + 4));
    std::string reply;
    ASSERT_TRUE(first.sendLine("PING"));
    ASSERT_TRUE(first.receiveLine(reply));
    first.close();
  }

  TcpClient second;
  ASSERT_TRUE(second.connect("127.0.0.1", kBasePort + 4));
  std::string reply;
  ASSERT_TRUE(second.sendLine("PING"));
  ASSERT_TRUE(second.receiveLine(reply));
  EXPECT_EQ(reply, "+OK");
  second.close();
}

TEST(NetworkingTest, WrongArityProducesAnErrorReply) {
  TestServer server(kBasePort + 5);
  ASSERT_TRUE(server.start());

  TcpClient client;
  ASSERT_TRUE(client.connect("127.0.0.1", kBasePort + 5));
  ASSERT_TRUE(client.sendLine("SET onlykey"));
  std::string reply;
  ASSERT_TRUE(client.receiveLine(reply));
  EXPECT_EQ(reply, "-ERR wrong number of arguments for 'SET'");

  client.close();
}

}  // namespace
