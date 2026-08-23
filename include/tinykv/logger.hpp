#pragma once

#include <string>

namespace tinykv {

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
 public:
  static void init(LogLevel level = LogLevel::INFO, const std::string& logFile = "");
  static void setLevel(LogLevel level);
  static void log(LogLevel level, const std::string& message);

 private:
  Logger() = default;
};

}  // namespace tinykv

#define LOG_DEBUG(msg) ::tinykv::Logger::log(::tinykv::LogLevel::DEBUG, (msg))
#define LOG_INFO(msg) ::tinykv::Logger::log(::tinykv::LogLevel::INFO, (msg))
#define LOG_WARN(msg) ::tinykv::Logger::log(::tinykv::LogLevel::WARN, (msg))
#define LOG_ERROR(msg) ::tinykv::Logger::log(::tinykv::LogLevel::ERROR, (msg))
