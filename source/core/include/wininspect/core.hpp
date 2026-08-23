#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "backend.hpp"
#include "tinyjson.hpp"
#include "logger.hpp"
#include "metrics.hpp"
#include "credential_manager.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>

namespace wininspect {

  struct CoreRequest
  {
    std::string id;
    std::string method;
    json::Object params;
  };

  struct CoreResponse
  {
    std::string id;
    bool ok = true;
    json::Value result;
    std::string error_code;
    std::string error_message;
    json::Object metrics;

    json::Object to_json_obj(bool canonical) const;
  };

  class CoreEngine
  {
  public:
    explicit CoreEngine(IBackend* backend);

    // Handle one request. O(1) dispatch via lookup table built at construction.
    [[nodiscard]] CoreResponse handle(const CoreRequest& req, const Snapshot& snapshot,
                                      const Snapshot* old_snapshot = nullptr);

    // Enable or disable admin-only methods (daemon.logs)
    IBackend* get_backend() const { return backend_; }
    void set_admin_logs_enabled(bool v) { admin_logs_enabled_ = v; }

    // Read-only mode — blocks all mutating methods
    void set_read_only(bool v) { read_only_ = v; }
    bool is_read_only() const { return read_only_; }

    // Optional audit hook — called after every dispatch when set.
    // Set by the daemon layer to log every RPC call to the audit trail.
    // Not coupled to ControlManager — CoreEngine stays testable.
    using AuditHook = std::function<void(const CoreRequest&, const CoreResponse&)>;
    void set_audit_hook(AuditHook hook) { audit_hook_ = std::move(hook); }

    // Daemon lifecycle state (set by the daemon main(), read by daemon.status)
    void set_daemon_state(const std::string& state) { daemon_state_ = state; }
    const std::string& get_daemon_state() const { return daemon_state_; }

    // License and deployment model (set by daemon main() from install.json)
    void set_license_info(const std::string& deployment, const std::string& license_type) {
      deployment_ = deployment;
      license_type_ = license_type;
    }
    const std::string& deployment() const { return deployment_; }
    const std::string& license_type() const { return license_type_; }

  private:
    using Handler =
        std::function<CoreResponse(const CoreRequest&, const Snapshot&, const Snapshot*)>;
    IBackend* backend_;
    std::unordered_map<std::string, Handler> dispatch_;
    bool admin_logs_enabled_ = false;
    void build_dispatch_table();

  public:
    MetricsCollector metrics_collector_;
    CredentialManager cred_mgr_;

    // Method policy: classifies every dispatch method
    struct MethodPolicy
    {
      bool mutates = false;             // modifies system state (blocked in read-only)
      bool sensitive = false;           // requires auth over network
      const char* capability = nullptr; // required runtime capability, null = none
    };
    static const std::unordered_map<std::string, MethodPolicy>& policy_table();

  private:
    AuditHook audit_hook_;
    std::string daemon_state_ = "init";
    std::string deployment_ = "interactive";
    std::string license_type_ = "noncommercial";
    bool read_only_ = false;

    // Session recording state
    struct RecordingState
    {
      std::atomic<bool> active{false};
      std::string path;
      int interval_ms = 1000;
      int max_frames = 0;
      int frame_count = 0;
      std::thread worker;

      ~RecordingState()
      {
        active = false;
        if (worker.joinable())
          worker.join();
      }
    };
    RecordingState recording_;
  };

  [[nodiscard]] CoreRequest parse_request_json(std::string_view json_utf8);
  [[nodiscard]] std::string serialize_response_json(const CoreResponse& resp, bool canonical);

} // namespace wininspect
