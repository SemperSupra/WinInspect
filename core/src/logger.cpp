#include "wininspect/logger.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace wininspect {

static std::string level_to_str(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERR:   return "ERROR";
        default: return "UNKNOWN";
    }
}

Logger& Logger::get() {
    static Logger instance;
    return instance;
}

void Logger::log(LogLevel level, const std::string& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
    std::string ts = ss.str();

    std::string formatted = "[" + ts + "] [" + level_to_str(level) + "] " + msg;

    if (level >= min_level_) {
        std::cerr << formatted << std::endl;
#ifdef _WIN32
        std::string win_msg = formatted + "\n";
        OutputDebugStringA(win_msg.c_str());
#endif
    }

    LogMessage lm{level, ts, msg};
    buffer_.push_back(lm);
    if (buffer_.size() > MAX_LOGS) {
        buffer_.erase(buffer_.begin());
    }
}

std::vector<LogMessage> Logger::get_recent_logs(size_t count) {
    std::lock_guard<std::mutex> lk(mu_);
    if (count >= buffer_.size()) return buffer_;
    return std::vector<LogMessage>(buffer_.end() - count, buffer_.end());
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lk(mu_);
    min_level_ = level;
}

} // namespace wininspect
