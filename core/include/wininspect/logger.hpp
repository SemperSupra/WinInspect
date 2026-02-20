#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

namespace wininspect {

enum class LogLevel {
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERR
};

struct LogMessage {
    LogLevel level;
    std::string timestamp;
    std::string message;
};

class Logger {
public:
    static Logger& get();

    void log(LogLevel level, const std::string& msg);
    std::vector<LogMessage> get_recent_logs(size_t count = 100);
    void set_level(LogLevel level);

private:
    Logger() = default;
    std::mutex mu_;
    LogLevel min_level_ = LogLevel::INFO;
    std::vector<LogMessage> buffer_;
    static constexpr size_t MAX_LOGS = 100;
};

#define LOG_TRACE(msg) Logger::get().log(LogLevel::TRACE, msg)
#define LOG_DEBUG(msg) Logger::get().log(LogLevel::DEBUG, msg)
#define LOG_INFO(msg)  Logger::get().log(LogLevel::INFO, msg)
#define LOG_WARN(msg)  Logger::get().log(LogLevel::WARN, msg)
#define LOG_ERROR(msg) Logger::get().log(LogLevel::ERR, msg)

} // namespace wininspect
