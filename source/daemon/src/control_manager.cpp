// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "control_manager.hpp"
#include "wininspect/logger.hpp"
#include "wininspect/tinyjson.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace wininspectd {

  ControlManager::ControlManager() {}
  ControlManager::~ControlManager() {}

  bool ControlManager::take_control(wininspect::ControllerType who, const std::string& id)
  {
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
    if (operation_mode_ == "human")
      return false;

    // If human has control, agent/script cannot take it
    if (controller_ == wininspect::ControllerType::Human)
      return false;

    // If no current controller, or agent/script wants to take from idle/self
    controller_ = who;
    controller_id_ = id;
    control_since_ = std::chrono::steady_clock::now();
    local_input_detected_ = false;
    return true;
  }

  bool ControlManager::release_control(wininspect::ControllerType who, const std::string& id)
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (controller_ == wininspect::ControllerType::None)
      return true;

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

  wininspect::ControllerType ControlManager::current_controller() const
  {
    std::lock_guard<std::mutex> lk(mu_);
    return controller_;
  }

  wininspect::json::Object ControlManager::get_status() const
  {
    std::lock_guard<std::mutex> lk(mu_);
    wininspect::json::Object o;
    o["controller"] = wininspect::controller_type_str(controller_);
    o["controller_id"] = controller_id_;
    o["operation_mode"] = operation_mode_;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now() - control_since_)
                       .count();
    o["control_duration_sec"] = (double)elapsed;
    o["local_input_detected"] = local_input_detected_.load();
    return o;
  }

  void ControlManager::log_action(const std::string& method, const wininspect::json::Object& params,
                                  bool ok, int64_t duration_ms)
  {
    std::lock_guard<std::mutex> lk(mu_);
    wininspect::AuditEntry entry;
    entry.seq = next_seq_++;
    auto now = std::chrono::system_clock::now();
    entry.timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
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

  wininspect::json::Array ControlManager::get_audit_log(size_t max_entries) const
  {
    std::lock_guard<std::mutex> lk(mu_);
    wininspect::json::Array arr;
    size_t start = audit_log_.size() > max_entries ? audit_log_.size() - max_entries : 0;
    for (size_t i = start; i < audit_log_.size(); i++) {
      const auto& e = audit_log_[i];
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

  void ControlManager::set_operation_mode(const std::string& mode)
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (mode == "auto" || mode == "hybrid" || mode == "human")
      operation_mode_ = mode;
  }

  std::string ControlManager::get_operation_mode() const
  {
    std::lock_guard<std::mutex> lk(mu_);
    return operation_mode_;
  }

  void ControlManager::save_audit_log(const std::string& path)
  {
    // Copy entries under lock, then serialize + write without lock
    std::vector<wininspect::AuditEntry> entries;
    {
      std::lock_guard<std::mutex> lk(mu_);
      entries = audit_log_;
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
      LOG_ERROR("Failed to open audit log: " + path);
      return;
    }
    for (const auto& e : entries) {
      wininspect::json::Object o;
      o["seq"] = (double)e.seq;
      o["timestamp"] = (double)e.timestamp;
      o["controller"] = e.controller;
      o["controller_id"] = e.controller_id;
      o["method"] = e.method;
      o["ok"] = e.ok;
      o["duration_ms"] = (double)e.duration_ms;
      out << wininspect::json::dumps(o) << "\n";
    }
  }

  void ControlManager::load_audit_log(const std::string& path)
  {
    std::lock_guard<std::mutex> lk(mu_);
    std::ifstream f(path);
    if (!f.is_open())
      return;

    audit_log_.clear();
    next_seq_ = 1;
    std::string line;
    while (std::getline(f, line)) {
      if (line.empty())
        continue;
      try {
        auto val = wininspect::json::parse(line);
        if (!val.is_obj())
          continue;
        auto& o = val.as_obj();
        wininspect::AuditEntry e;
        auto it = o.find("seq");
        if (it != o.end() && it->second.is_num())
          e.seq = (uint64_t)it->second.as_num();
        it = o.find("timestamp");
        if (it != o.end() && it->second.is_num())
          e.timestamp = (int64_t)it->second.as_num();
        it = o.find("controller");
        if (it != o.end() && it->second.is_str())
          e.controller = it->second.as_str();
        it = o.find("controller_id");
        if (it != o.end() && it->second.is_str())
          e.controller_id = it->second.as_str();
        it = o.find("method");
        if (it != o.end() && it->second.is_str())
          e.method = it->second.as_str();
        it = o.find("ok");
        if (it != o.end() && it->second.is_bool())
          e.ok = it->second.as_bool();
        it = o.find("duration_ms");
        if (it != o.end() && it->second.is_num())
          e.duration_ms = (int64_t)it->second.as_num();
        audit_log_.push_back(std::move(e));
      }
      catch (...) { /* skip malformed lines */
      }
    }

    // Restore next_seq
    for (const auto& e : audit_log_) {
      if (e.seq >= next_seq_)
        next_seq_ = e.seq + 1;
    }
  }

  bool ControlManager::can_inject_input() const
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (operation_mode_ == "human")
      return false;
    if (controller_ == wininspect::ControllerType::Human)
      return false;
    // In auto mode, agents can always inject
    // In hybrid mode, agents can inject if no local input detected
    if (operation_mode_ == "auto")
      return true;
    if (operation_mode_ == "hybrid")
      return !local_input_detected_.load();
    return false;
  }

  void ControlManager::notify_local_input()
  {
    local_input_detected_ = true;
    // Local input always seizes control from agent/automation, regardless of mode.
    // This ensures a user at the keyboard can always take over — even in fleet mode
    // where automation is the primary controller. Human control is the last resort.
    std::lock_guard<std::mutex> lk(mu_);
    if (controller_ != wininspect::ControllerType::Human && controller_ != wininspect::ControllerType::None) {
      controller_ = wininspect::ControllerType::None;
      controller_id_.clear();
    }
  }

} // namespace wininspectd
