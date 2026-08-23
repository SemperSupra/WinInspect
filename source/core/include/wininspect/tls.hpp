#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace wininspect {

  /// TLS 1.3 session wrapper using OpenSSL.
  ///
  /// Provides encrypted send/recv over an already-connected socket.
  /// When compiled without WININSPECT_HAVE_OPENSSL, all methods return
  /// false and log errors — the caller should fall back gracefully.
  class TlsSession
  {
  public:
    TlsSession();
    ~TlsSession();

    /// Initialize as server-side TLS with certificate and private key in PEM.
    [[nodiscard]] bool init_server(const std::string& cert_pem, const std::string& key_pem);

    /// Initialize as client-side TLS (anonymous, no client cert).
    [[nodiscard]] bool init_client();

    /// Perform TLS handshake on an already-connected socket.
    [[nodiscard]] bool handshake(uintptr_t socket);

    /// Send encrypted data. Returns true if all bytes were sent.
    [[nodiscard]] bool send(uintptr_t socket, const std::vector<uint8_t>& data);

    /// Receive decrypted data. Returns true if data was read.
    [[nodiscard]] bool recv(uintptr_t socket, std::vector<uint8_t>& data);

    /// True after a successful handshake.
    [[nodiscard]] bool is_initialized() const;

    /// Generate a self-signed EC (P-256) certificate and key in PEM format.
    /// Useful for LAN deployments where a CA-signed cert is not available.
    [[nodiscard]] static bool generate_self_signed_cert(const std::string& subject,
                                                        std::string& out_cert_pem,
                                                        std::string& out_key_pem);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace wininspect
