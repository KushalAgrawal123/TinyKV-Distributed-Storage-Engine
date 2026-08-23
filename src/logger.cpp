#include "tinykv/logger.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace tinykv {

namespace {

std::mutex g_logMutex;
LogLevel g_level = LogLevel::INFO;
std::ofstream g_logFile;
bool g_useFile = false;

const char* levelName(LogLevel level) {
  switch (level) {
    case LogLevel::DEBUG:
      return "DEBUG";
    case LogLevel::INFO:
      return "INFO";
    case LogLevel::WARN:
      return "WARN";
    case LogLevel::ERROR:
      return "ERROR";
  }
  return "UNKNOWN";
}

std::string timestamp() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tmBuf{};
  localtime_r(&t, &tmBuf);
  std::ostringstream oss;
  oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

}  // namespace

void Logger::init(LogLevel level, const std::string& logFile) {
  std::lock_guard<std::mutex> lock(g_logMutex);
  g_level = level;
  if (!logFile.empty()) {
    g_logFile.open(logFile, std::ios::app);
    g_useFile = g_logFile.is_open();
  }
}

void Logger::setLevel(LogLevel level) {
  std::lock_guard<std::mutex> lock(g_logMutex);
  g_level = level;
}

void Logger::log(LogLevel level, const std::string& message) {
  std::lock_guard<std::mutex> lock(g_logMutex);
  if (level < g_level) return;

  std::string line = "[" + timestamp() + "] [" + levelName(level) + "] " + message;
  std::ostream& out = (level == LogLevel::ERROR) ? std::cerr : std::cout;
  out << line << std::endl;
  if (g_useFile) {
    g_logFile << line << std::endl;
  }
}

}  // namespace tinykv
