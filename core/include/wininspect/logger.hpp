#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

namespace wininspect {

enum class LogLevel : std::uint8_t {
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

    bool should_log(LogLevel level) const;
    void log(LogLevel level, const std::string& msg);
    std::vector<LogMessage> get_recent_logs(size_t count = 100);
    void set_level(LogLevel level);

private:
    Logger() = default;
    mutable std::mutex mu_;
    LogLevel min_level_ = LogLevel::INFO;
    std::vector<LogMessage> buffer_;
    static constexpr size_t MAX_LOGS = 100;
};

#define LOG_AT_LEVEL(level, msg) \
    do { if (wininspect::Logger::get().should_log(level)) wininspect::Logger::get().log(level, msg); } while(0)

#define LOG_TRACE(msg) LOG_AT_LEVEL(wininspect::LogLevel::TRACE, msg)
#define LOG_DEBUG(msg) LOG_AT_LEVEL(wininspect::LogLevel::DEBUG, msg)
#define LOG_INFO(msg)  LOG_AT_LEVEL(wininspect::LogLevel::INFO, msg)
#define LOG_WARN(msg)  LOG_AT_LEVEL(wininspect::LogLevel::WARN, msg)
#define LOG_ERROR(msg) LOG_AT_LEVEL(wininspect::LogLevel::ERR, msg)

} // namespace wininspect
