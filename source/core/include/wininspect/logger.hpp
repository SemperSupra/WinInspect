#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <fstream>

namespace wininspect {

  enum class LogLevel : std::uint8_t { TRACE = 0, DEBUG, INFO, WARN, ERR };

  struct LogMessage
  {
    LogLevel level;
    std::string timestamp;
    std::string message;
  };

  class Logger
  {
  public:
    static Logger& get();

    void log(LogLevel level, const std::string& msg);
    std::vector<LogMessage> get_recent_logs(size_t count = 100);
    void set_level(LogLevel level);
    LogLevel get_level() const;
    void set_log_file(const std::string& path);

    /// Set log directory (auto-creates, uses timestamped filenames).
    /// Rotates at max_log_size bytes (default 10 MB).
    void set_log_dir(const std::string& dir, size_t max_size = 10 * 1024 * 1024);

  private:
    Logger() = default;
    void rotate_log(); // rotate current log file if over limit
    mutable std::mutex mu_;
    LogLevel min_level_ = LogLevel::INFO;
    std::vector<LogMessage> buffer_;
    std::ofstream log_file_;
    std::string log_dir_;
    std::string log_file_path_;
    size_t max_log_size_ = 10 * 1024 * 1024;
    static constexpr size_t MAX_LOGS = 100;
  };

// LOG_AT_LEVEL calls log() directly; log() checks the level filter internally.
// Never call should_log() before log() — it causes a deadlock on std::mutex.
#define LOG_AT_LEVEL(level, msg)                                                                   \
  do {                                                                                             \
    wininspect::Logger::get().log(level, msg);                                                     \
  } while (0)

#define LOG_TRACE(msg) LOG_AT_LEVEL(wininspect::LogLevel::TRACE, msg)
#define LOG_DEBUG(msg) LOG_AT_LEVEL(wininspect::LogLevel::DEBUG, msg)
#define LOG_INFO(msg) LOG_AT_LEVEL(wininspect::LogLevel::INFO, msg)
#define LOG_WARN(msg) LOG_AT_LEVEL(wininspect::LogLevel::WARN, msg)
#define LOG_ERROR(msg) LOG_AT_LEVEL(wininspect::LogLevel::ERR, msg)

} // namespace wininspect
