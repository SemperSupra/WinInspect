#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "backend.hpp"
#include "tinyjson.hpp"
#include "logger.hpp"
#include <functional>
#include <string>
#include <unordered_map>

namespace wininspect {

struct CoreRequest {
  std::string id;
  std::string method;
  json::Object params;
};

struct CoreResponse {
  std::string id;
  bool ok = true;
  json::Value result;
  std::string error_code;
  std::string error_message;
  json::Object metrics;

  json::Object to_json_obj(bool canonical) const;
};

class CoreEngine {
public:
  explicit CoreEngine(IBackend *backend);

  // Handle one request. O(1) dispatch via lookup table built at construction.
  [[nodiscard]] CoreResponse handle(const CoreRequest &req, const Snapshot &snapshot,
                                     const Snapshot *old_snapshot = nullptr);

  // Enable or disable admin-only methods (daemon.logs)
  IBackend *get_backend() const { return backend_; }
  void set_admin_logs_enabled(bool v) { admin_logs_enabled_ = v; }

private:
  using Handler = std::function<CoreResponse(const CoreRequest&,
                                             const Snapshot&, const Snapshot*)>;
  IBackend *backend_;
  std::unordered_map<std::string, Handler> dispatch_;
  bool admin_logs_enabled_ = false;
  void build_dispatch_table();
};

[[nodiscard]] CoreRequest parse_request_json(std::string_view json_utf8);
[[nodiscard]] std::string serialize_response_json(const CoreResponse &resp, bool canonical);

} // namespace wininspect
