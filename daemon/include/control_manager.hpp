#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/types.hpp"
#include "wininspect/tinyjson.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <chrono>

namespace wininspectd {

/// Tracks who is in control of the daemon and maintains an audit log.
class ControlManager {
public:
  ControlManager();
  ~ControlManager();

  /// Take control. Returns false if cannot take (e.g., human won't release).
  bool take_control(wininspect::ControllerType who, const std::string &id = "");

  /// Release control. Only the current controller can release (except human).
  bool release_control(wininspect::ControllerType who, const std::string &id = "");

  /// Get current control state as JSON.
  wininspect::json::Object get_status() const;

  /// Log an action to the audit trail.
  void log_action(const std::string &method, const wininspect::json::Object &params,
                   bool ok, int64_t duration_ms);

  /// Get audit log (newest first, up to max_entries).
  wininspect::json::Array get_audit_log(size_t max_entries = 100) const;

  /// Set operation mode.
  void set_operation_mode(const std::string &mode);  // "auto", "hybrid", "human"
  std::string get_operation_mode() const;

  /// Check if an agent/script can perform input injection.
  bool can_inject_input() const;

  /// Notify that local input was detected (human took over).
  void notify_local_input();

private:
  mutable std::mutex mu_;
  wininspect::ControllerType controller_{wininspect::ControllerType::None};
  std::string controller_id_;
  std::chrono::steady_clock::time_point control_since_;
  std::vector<wininspect::AuditEntry> audit_log_;
  uint64_t next_seq_ = 1;
  std::string operation_mode_ = "hybrid";
  size_t max_audit_entries_ = 10000;
  std::atomic<bool> local_input_detected_{false};
};

} // namespace wininspectd
