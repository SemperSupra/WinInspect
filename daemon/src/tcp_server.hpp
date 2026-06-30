#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include <atomic>
#include <functional>
#include <string>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#endif
#include "server_state.hpp"
#include "wininspect/network_config.hpp"

namespace wininspect {
class IBackend;
} // namespace wininspect

namespace wininspectd {

class TcpServer {
public:
  TcpServer(wininspect::ServerState *state,
            wininspect::IBackend *backend);
  ~TcpServer();

  void start(std::atomic<bool> *running,
             const wininspect::NetworkConfig &cfg,
             const std::string &auth_keys = "",
             bool read_only = false,
             bool admin_logs = false,
             bool no_clipboard = false);
  void stop();

private:
  wininspect::ServerState *state_;
  wininspect::IBackend *backend_;
  std::vector<uintptr_t> listen_socks_;
};

} // namespace wininspectd
