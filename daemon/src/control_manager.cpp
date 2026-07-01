// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "control_manager.hpp"
#include <algorithm>

namespace wininspectd {

ControlManager::ControlManager() {}
ControlManager::~ControlManager() {}

bool ControlManager::take_control(wininspect::ControllerType who,
                                   const std::string &id) {
  std::lock_guard<std::mutex> lk(mu_);

  // Human always wins
  if (who == wininspect::ControllerType::Human) {
    controller_ = who;
    controller_id_ = id;
    control_since_ = std::chrono::steady_clock::now();
    local_input_detected_ = false;
    return true;
  }

  // In human-only mode, agents/scripts cannot take control
  if (operation_mode_ == "human") return false;

  // If human has control, agent/script cannot take it
  if (controller_ == wininspect::ControllerType::Human) return false;

  // If no current controller, or agent/script wants to take from idle/self
  controller_ = who;
  controller_id_ = id;
  control_since_ = std::chrono::steady_clock::now();
  local_input_detected_ = false;
  return true;
}

bool ControlManager::release_control(wininspect::ControllerType who,
                                      const std::string &id) {
  std::lock_guard<std::mutex> lk(mu_);
  if (controller_ == wininspect::ControllerType::None) return true;

  // Human can always release themselves or anyone else
  if (who == wininspect::ControllerType::Human) {
    controller_ = wininspect::ControllerType::None;
    controller_id_.clear();
    return true;
  }

  // Agent/script can only release themselves
  if (who == controller_ && (id.empty() || id == controller_id_)) {
    controller_ = wininspect::ControllerType::None;
    controller_id_.clear();
    return true;
  }

  return false;
}

wininspect::json::Object ControlManager::get_status() const {
  std::lock_guard<std::mutex> lk(mu_);
  wininspect::json::Object o;
  o["controller"] = wininspect::controller_type_str(controller_);
  o["controller_id"] = controller_id_;
  o["operation_mode"] = operation_mode_;
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - control_since_).count();
  o["control_duration_sec"] = (double)elapsed;
  o["local_input_detected"] = local_input_detected_.load();
  return o;
}

void ControlManager::log_action(const std::string &method,
                                 const wininspect::json::Object &params,
                                 bool ok, int64_t duration_ms) {
  std::lock_guard<std::mutex> lk(mu_);
  wininspect::AuditEntry entry;
  entry.seq = next_seq_++;
  auto now = std::chrono::system_clock::now();
  entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()).count();
  entry.controller = wininspect::controller_type_str(controller_);
  entry.controller_id = controller_id_;
  entry.method = method;
  entry.params = params;
  entry.ok = ok;
  entry.duration_ms = duration_ms;
  audit_log_.push_back(std::move(entry));

  // Trim to max size
  while (audit_log_.size() > max_audit_entries_)
    audit_log_.erase(audit_log_.begin());
}

wininspect::json::Array ControlManager::get_audit_log(size_t max_entries) const {
  std::lock_guard<std::mutex> lk(mu_);
  wininspect::json::Array arr;
  size_t start = audit_log_.size() > max_entries
      ? audit_log_.size() - max_entries : 0;
  for (size_t i = start; i < audit_log_.size(); i++) {
    const auto &e = audit_log_[i];
    wininspect::json::Object o;
    o["seq"] = (double)e.seq;
    o["timestamp"] = (double)e.timestamp;
    o["controller"] = e.controller;
    o["controller_id"] = e.controller_id;
    o["method"] = e.method;
    o["ok"] = e.ok;
    o["duration_ms"] = (double)e.duration_ms;
    arr.push_back(o);
  }
  return arr;
}

void ControlManager::set_operation_mode(const std::string &mode) {
  std::lock_guard<std::mutex> lk(mu_);
  if (mode == "auto" || mode == "hybrid" || mode == "human")
    operation_mode_ = mode;
}

std::string ControlManager::get_operation_mode() const {
  std::lock_guard<std::mutex> lk(mu_);
  return operation_mode_;
}

bool ControlManager::can_inject_input() const {
  std::lock_guard<std::mutex> lk(mu_);
  if (operation_mode_ == "human") return false;
  if (controller_ == wininspect::ControllerType::Human) return false;
  // In auto mode, agents can always inject
  // In hybrid mode, agents can inject if no local input detected
  if (operation_mode_ == "auto") return true;
  if (operation_mode_ == "hybrid")
    return !local_input_detected_.load();
  return false;
}

void ControlManager::notify_local_input() {
  local_input_detected_ = true;
  // Auto-release agent if local input detected in hybrid mode
  if (operation_mode_ == "hybrid" &&
      controller_ != wininspect::ControllerType::Human) {
    std::lock_guard<std::mutex> lk(mu_);
    controller_ = wininspect::ControllerType::None;
    controller_id_.clear();
  }
}

} // namespace wininspectd
