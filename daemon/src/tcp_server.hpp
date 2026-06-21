#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include <atomic>
#include <functional>
#include <string>
#include "server_state.hpp"

namespace wininspect {
class IBackend;
} // namespace wininspect

namespace wininspectd {

class TcpServer {
public:
  TcpServer(int port, wininspect::ServerState *state,
            wininspect::IBackend *backend);
  ~TcpServer();

  void start(std::atomic<bool> *running, bool bind_public = false,
             const std::string &auth_keys = "", bool read_only = false,
             bool admin_logs = false);
  void stop();

private:
  // RAII wrapper for Winsock — constructed once, ref-counted by the OS if
  // start() is called multiple times (tray fallback path).
  struct WsaGuard {
    WsaGuard();
    ~WsaGuard();
    bool ok() const { return ok_; }
  private:
    bool ok_ = false;
  };

  int port_;
  wininspect::ServerState *state_;
  wininspect::IBackend *backend_;
  uintptr_t listen_sock_ = 0;
};

} // namespace wininspectd
