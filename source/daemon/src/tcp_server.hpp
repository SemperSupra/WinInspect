#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
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

  class TcpServer
  {
  public:
    TcpServer(wininspect::ServerState* state, wininspect::IBackend* backend);
    ~TcpServer();

    void start(std::atomic<bool>* running, const wininspect::NetworkConfig& cfg,
               const std::string& auth_keys = "", bool read_only = false, bool admin_logs = false,
               bool no_clipboard = false);

    /// Start TLS-wrapped TCP listener on cfg.tls_port.
    /// Accepts connections, performs TLS 1.3 handshake, then dispatches
    /// to handle_socket_client with TlsSession for encrypted I/O.
    void start_tls(std::atomic<bool>* running, const wininspect::NetworkConfig& cfg,
                   const std::string& cert_pem, const std::string& key_pem,
                   const std::string& auth_keys = "", bool read_only = false,
                   bool admin_logs = false, bool no_clipboard = false);
    void stop();

  private:
    wininspect::ServerState* state_;
    wininspect::IBackend* backend_;
    std::vector<uintptr_t> listen_socks_;
  };

} // namespace wininspectd
