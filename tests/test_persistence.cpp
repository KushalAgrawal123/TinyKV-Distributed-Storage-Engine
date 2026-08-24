#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "tinykv/command_executor.hpp"
#include "tinykv/concurrency/thread_pool.hpp"
#include "tinykv/persistence/aof.hpp"
#include "tinykv/persistence/persistence_manager.hpp"
#include "tinykv/persistence/snapshot.hpp"
#include "tinykv/protocol/parser.hpp"
#include "tinykv/storage/expiry_manager.hpp"
#include "tinykv/storage/kv_store.hpp"

using namespace tinykv;
namespace fs = std::filesystem;

namespace {

// Each test gets its own scratch directory, wiped clean up front, so no
// test can see another's leftover files (the exact bug that once made
// scripts/smoke_test.sh flaky - see its own comments).
fs::path freshTempDir(const std::string& name) {
  fs::path dir = fs::temp_directory_path() / ("tinykv_gtest_" + name);
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  return dir;
}

}  // namespace

TEST(PersistenceTest, AofWriterAppendsThenLoaderReplaysThroughExecutor) {
  fs::path dir = freshTempDir("aof_roundtrip");
  std::string aofPath = (dir / "test.aof").string();

  {
    AofWriter writer(aofPath);
    writer.append("SET foo bar");
    writer.append("SET baz qux");
    writer.append("DEL foo");
    writer.flush();
  }

  KVStore store;
  ExpiryManager expiry;
  CommandExecutor executor(store, expiry);
  ASSERT_TRUE(AofLoader::replay(aofPath, executor));

  EXPECT_FALSE(store.exists("foo"));
  EXPECT_EQ(*store.get("baz"), "qux");
}

TEST(PersistenceTest, AofLoaderOnMissingFileReturnsFalseNotAnError) {
  KVStore store;
  ExpiryManager expiry;
  CommandExecutor executor(store, expiry);
  EXPECT_FALSE(AofLoader::replay("/nonexistent/path/that/wont/exist.aof", executor));
}

TEST(PersistenceTest, AofResetTruncatesTheFile) {
  fs::path dir = freshTempDir("aof_reset");
  std::string aofPath = (dir / "test.aof").string();

  AofWriter writer(aofPath);
  writer.append("SET foo bar");
  writer.flush();
  writer.reset();
  writer.append("SET after reset");
  writer.flush();

  std::ifstream in(aofPath);
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_EQ(contents.find("foo"), std::string::npos);
  EXPECT_NE(contents.find("after"), std::string::npos);
}

TEST(PersistenceTest, SnapshotSaveThenLoadRoundTripsAllKeys) {
  KVStore store;
  store.set("a", "1");
  store.set("b", "2");
  store.set("c", "3");

  std::ostringstream out;
  SnapshotManager::save(out, store);

  KVStore loadedStore;
  ExpiryManager expiry;
  CommandExecutor executor(loadedStore, expiry);
  std::istringstream in(out.str());
  SnapshotManager::load(in, executor);

  EXPECT_EQ(loadedStore.size(), 3u);
  EXPECT_EQ(*loadedStore.get("a"), "1");
  EXPECT_EQ(*loadedStore.get("b"), "2");
  EXPECT_EQ(*loadedStore.get("c"), "3");
}

TEST(PersistenceTest, SnapshotFileRoundTrip) {
  fs::path dir = freshTempDir("snapshot_file");
  std::string path = (dir / "dump.tkv").string();

  KVStore store;
  store.set("k", "v");
  ASSERT_TRUE(SnapshotManager::saveToFile(path, store));

  KVStore loaded;
  ExpiryManager expiry;
  CommandExecutor executor(loaded, expiry);
  ASSERT_TRUE(SnapshotManager::loadFromFile(path, executor));
  EXPECT_EQ(*loaded.get("k"), "v");
}

TEST(PersistenceTest, LoadFromMissingSnapshotFileReturnsFalse) {
  KVStore store;
  ExpiryManager expiry;
  CommandExecutor executor(store, expiry);
  EXPECT_FALSE(SnapshotManager::loadFromFile("/nonexistent/path/dump.tkv", executor));
}

// The actual recovery contract: SAVE captures a point-in-time snapshot
// and resets the AOF, so a crash afterward is recovered as
// "snapshot, then replay whatever writes landed in the AOF since" - not
// "replay everything from the start of time".
TEST(PersistenceTest, RecoverReplaysSnapshotThenOnlyTheAofWritesSinceIt) {
  fs::path dir = freshTempDir("recover");

  // First "session".
  {
    KVStore store;
    ExpiryManager expiry;
    CommandExecutor executor(store, expiry);
    ThreadPool pool(1);
    PersistenceConfig config;
    config.dir = dir.string();
    config.saveIntervalSeconds = 0;
    PersistenceManager persistence(config, store, executor, pool);

    executor.execute(Parser::parse("SET snapkey snapval"));
    persistence.save();  // captures snapkey in a snapshot, resets the AOF

    executor.execute(Parser::parse("SET aofkey aofval"));
    persistence.recordWrite("SET aofkey aofval");  // only in the AOF

    executor.execute(Parser::parse("DEL snapkey"));
    persistence.recordWrite("DEL snapkey");  // also only in the AOF
  }

  // Second "session": fresh KVStore, recover from what's on disk.
  KVStore recoveredStore;
  ExpiryManager recoveredExpiry;
  CommandExecutor recoveredExecutor(recoveredStore, recoveredExpiry);
  ThreadPool pool2(1);
  PersistenceConfig config2;
  config2.dir = dir.string();
  config2.saveIntervalSeconds = 0;
  PersistenceManager persistence2(config2, recoveredStore, recoveredExecutor, pool2);
  persistence2.recover();

  EXPECT_FALSE(recoveredStore.exists("snapkey")) << "the AOF's DEL, replayed after the snapshot, should have removed it";
  EXPECT_EQ(*recoveredStore.get("aofkey"), "aofval");
}

TEST(PersistenceTest, RecordWriteIsANoOpWhenAppendOnlyIsDisabled) {
  fs::path dir = freshTempDir("appendonly_off");

  KVStore store;
  ExpiryManager expiry;
  CommandExecutor executor(store, expiry);
  ThreadPool pool(1);
  PersistenceConfig config;
  config.dir = dir.string();
  config.appendOnly = false;
  config.saveIntervalSeconds = 0;
  PersistenceManager persistence(config, store, executor, pool);

  executor.execute(Parser::parse("SET foo bar"));
  persistence.recordWrite("SET foo bar");

  std::ifstream in((dir / "tinykv.aof").string());
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_TRUE(contents.empty());
}
