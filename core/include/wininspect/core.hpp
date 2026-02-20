#pragma once
#include "backend.hpp"
#include "tinyjson.hpp"
#include "logger.hpp"
#include <string>

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

  // Handle one request. Core itself is stateless; snapshot state lives in
  // daemon layer.
  CoreResponse handle(const CoreRequest &req, const Snapshot &snapshot,
                      const Snapshot *old_snapshot = nullptr);

private:
  IBackend *backend_;
};

CoreRequest parse_request_json(std::string_view json_utf8);
std::string serialize_response_json(const CoreResponse &resp, bool canonical);

} // namespace wininspect
