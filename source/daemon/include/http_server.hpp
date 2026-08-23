#pragma once

#include "wininspect/core.hpp"
#include <atomic>
#include <string>

#include "server_state.hpp"

namespace wininspectd {

  void run_http_server(std::atomic<bool>* running, int port, wininspect::CoreEngine& core,
                       wininspect::MetricsCollector* daemon_metrics, ServerState* state,
                       const std::string& auth_token, bool read_only = false,
                       bool no_clipboard = false, int redirect_https_port = 0);

  void run_https_server(std::atomic<bool>* running, int port, wininspect::CoreEngine& core,
                        const std::string& auth_token, bool read_only, bool no_clipboard,
                        const std::string& cert_pem, const std::string& key_pem);

} // namespace wininspectd
