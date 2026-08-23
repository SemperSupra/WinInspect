// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/logger.hpp"
#include "wininspect/credential_manager.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
// Event source name registered in the NSIS installer under
// HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\WinInspect
static constexpr const wchar_t* EVENT_SOURCE = L"WinInspect";
#endif

namespace wininspect {

  static std::string level_to_str(LogLevel level)
  {
    switch (level) {
    case LogLevel::TRACE:
      return "TRACE";
    case LogLevel::DEBUG:
      return "DEBUG";
    case LogLevel::INFO:
      return "INFO";
    case LogLevel::WARN:
      return "WARN";
    case LogLevel::ERR:
      return "ERROR";
    default:
      return "UNKNOWN";
    }
  }

  Logger& Logger::get()
  {
    static Logger instance;
    return instance;
  }

  void Logger::log(LogLevel level, const std::string& msg)
  {
    std::lock_guard<std::mutex> lk(mu_);

    // Redact known credential targets from all log output
    static CredentialManager cred_mgr;
    std::string redacted = cred_mgr.redact(msg);

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
    std::string ts = ss.str();

    if (level >= min_level_) {
      std::string formatted = "[" + ts + "] [" + level_to_str(level) + "] " + redacted;
      std::cerr << formatted << std::endl;
      if (log_file_.is_open()) {
        rotate_log();
        log_file_ << formatted << std::endl;
      }
#ifdef _WIN32
      std::string win_msg = formatted + "\n";
      OutputDebugStringA(win_msg.c_str());
      // Windows Event Log (WARN and ERROR only — avoid flooding Event Viewer)
      if (level >= LogLevel::WARN) {
        HANDLE hEvt = RegisterEventSourceW(nullptr, EVENT_SOURCE);
        if (hEvt) {
          // Convert to wide for ReportEventW
          int wlen = MultiByteToWideChar(CP_UTF8, 0, redacted.c_str(), -1, nullptr, 0);
          std::wstring wmsg(wlen, L'\0');
          MultiByteToWideChar(CP_UTF8, 0, redacted.c_str(), -1, &wmsg[0], wlen);
          const wchar_t* strings[] = {wmsg.c_str()};
          WORD evtType = (level == LogLevel::ERR) ? EVENTLOG_ERROR_TYPE : EVENTLOG_WARNING_TYPE;
          ReportEventW(hEvt, evtType, 0, 0, nullptr, 1, 0, strings, nullptr);
          DeregisterEventSource(hEvt);
        }
      }
#endif
    }

    buffer_.push_back({level, ts, redacted});
    if (buffer_.size() > MAX_LOGS) {
      buffer_.erase(buffer_.begin());
    }
  }

  std::vector<LogMessage> Logger::get_recent_logs(size_t count)
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (count >= buffer_.size())
      return buffer_;
    return std::vector<LogMessage>(buffer_.end() - count, buffer_.end());
  }

  void Logger::set_level(LogLevel level)
  {
    std::lock_guard<std::mutex> lk(mu_);
    min_level_ = level;
  }

  void Logger::set_log_file(const std::string& path)
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (log_file_.is_open())
      log_file_.close();
    log_file_.open(path, std::ios::app);
    log_file_path_ = path;
    if (!log_file_.is_open()) {
      std::cerr << "[Logger] Failed to open log file: " << path << std::endl;
    }
  }

  void Logger::set_log_dir(const std::string& dir, size_t max_size)
  {
    std::lock_guard<std::mutex> lk(mu_);
    log_dir_ = dir;
    max_log_size_ = max_size;

    // Create directory if needed
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
      std::cerr << "[Logger] Failed to create log directory: " << dir << std::endl;
      return;
    }

    // Generate timestamped filename
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "wininspect-%Y-%m-%d.log", std::localtime(&in_time_t));
    std::string path = dir + "/" + buf;

    if (log_file_.is_open())
      log_file_.close();
    log_file_.open(path, std::ios::app);
    log_file_path_ = path;
    if (!log_file_.is_open()) {
      std::cerr << "[Logger] Failed to open log file: " << path << std::endl;
    }
  }

  void Logger::rotate_log()
  {
    // Only rotate if file exists and exceeds max_log_size_
    if (log_file_path_.empty() || !log_file_.is_open())
      return;
    std::error_code ec;
    auto fsize = std::filesystem::file_size(log_file_path_, ec);
    if (ec || fsize < max_log_size_)
      return;

    // Close current file, rename to .1, open fresh
    log_file_.close();
    std::string rotated = log_file_path_ + ".1";
    std::filesystem::remove(rotated, ec);
    std::filesystem::rename(log_file_path_, rotated, ec);
    log_file_.open(log_file_path_, std::ios::app);
    if (!log_file_.is_open()) {
      std::cerr << "[Logger] Failed to reopen log after rotation" << std::endl;
    }
  }

} // namespace wininspect
