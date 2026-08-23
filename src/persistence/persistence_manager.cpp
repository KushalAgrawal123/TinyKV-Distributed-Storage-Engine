#include "tinykv/persistence/persistence_manager.hpp"

#include <filesystem>

#include "tinykv/logger.hpp"
#include "tinykv/persistence/snapshot.hpp"

namespace tinykv {

namespace {

// AofWriter opens its file immediately in its constructor and has no
// default constructor (it owns a std::mutex, so it can't be
// default-constructed-then-reassigned either) - this makes sure the
// directory exists before that open() call, computing aofWriter_'s
// constructor argument as part of the same initializer-list expression.
std::string ensureDirAndAofPath(const PersistenceConfig& config) {
  std::filesystem::create_directories(config.dir);
  return config.dir + "/" + config.appendFilename;
}

}  // namespace

PersistenceManager::PersistenceManager(PersistenceConfig config, KVStore& store, CommandExecutor& executor,
                                        ThreadPool& pool)
    : config_(std::move(config)),
      store_(store),
      executor_(executor),
      pool_(pool),
      aofWriter_(ensureDirAndAofPath(config_)) {}

PersistenceManager::~PersistenceManager() { stop(); }

std::string PersistenceManager::snapshotPath() const { return config_.dir + "/" + config_.dbFilename; }

std::string PersistenceManager::aofPath() const { return config_.dir + "/" + config_.appendFilename; }

void PersistenceManager::recover() {
  bool loadedSnapshot = SnapshotManager::loadFromFile(snapshotPath(), executor_);
  LOG_INFO(loadedSnapshot ? "Loaded snapshot from " + snapshotPath() : "No snapshot found at " + snapshotPath());

  if (config_.appendOnly) {
    bool replayedAof = AofLoader::replay(aofPath(), executor_);
    LOG_INFO(replayedAof ? "Replayed AOF from " + aofPath() : "No AOF found at " + aofPath());
  }
}

void PersistenceManager::recordWrite(const std::string& requestLine) {
  if (!config_.appendOnly) return;
  aofWriter_.append(requestLine);
}

void PersistenceManager::save() {
  std::lock_guard<std::mutex> lock(saveMutex_);
  SnapshotManager::saveToFile(snapshotPath(), store_);
  aofWriter_.reset();
  LOG_INFO("Snapshot saved to " + snapshotPath() + "; AOF reset");
}

void PersistenceManager::submitBackgroundTask(std::function<void()> task) {
  ++inFlightTasks_;
  pool_.submit([this, task = std::move(task)]() {
    task();
    if (--inFlightTasks_ == 0) {
      std::lock_guard<std::mutex> lock(tasksDoneMutex_);
      tasksDoneCv_.notify_all();
    }
  });
}

void PersistenceManager::start() {
  {
    std::lock_guard<std::mutex> lock(cvMutex_);
    running_ = true;
  }
  timerThread_ = std::thread([this] { timerLoop(); });
}

void PersistenceManager::stop() {
  {
    std::lock_guard<std::mutex> lock(cvMutex_);
    if (!running_) return;
    running_ = false;
  }
  cv_.notify_all();
  if (timerThread_.joinable()) timerThread_.join();

  // The timer thread is guaranteed stopped above, so no new background
  // tasks can be submitted past this point - safe to wait for any
  // already-submitted ones (which capture `this`) to actually finish
  // before this object's members get destroyed.
  std::unique_lock<std::mutex> lock(tasksDoneMutex_);
  tasksDoneCv_.wait(lock, [this] { return inFlightTasks_.load() == 0; });
}

void PersistenceManager::timerLoop() {
  int secondsSinceSnapshot = 0;
  std::unique_lock<std::mutex> lock(cvMutex_);
  while (running_) {
    if (cv_.wait_for(lock, std::chrono::seconds(1), [this] { return !running_; })) {
      break;
    }

    submitBackgroundTask([this] { aofWriter_.flush(); });

    ++secondsSinceSnapshot;
    if (config_.saveIntervalSeconds > 0 && secondsSinceSnapshot >= config_.saveIntervalSeconds) {
      secondsSinceSnapshot = 0;
      submitBackgroundTask([this] {
        save();
        LOG_INFO("Automatic snapshot saved");
      });
    }
  }
}

}  // namespace tinykv
