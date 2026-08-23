#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace wininspect {

  /// WebSocket frame opcodes
  enum class WsOpcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA
  };

  /// Compute the Sec-WebSocket-Accept value from the client's key.
  std::string ws_accept_key(const std::string& key);

  /// Build an HTTP 101 Switching Protocols response for WebSocket upgrade.
  std::string ws_upgrade_response(const std::string& key);

  /// Check if an HTTP request is a WebSocket upgrade.
  bool ws_is_upgrade(const std::string& method, const std::string& connection_hdr,
                     const std::string& upgrade_hdr);

  /// Encode a WebSocket frame (unmasked, for server -> client).
  std::vector<uint8_t> ws_encode_frame(WsOpcode opcode, const std::string& payload);

  /// Send a WebSocket frame over a socket. Returns true on success.
  bool ws_send_frame(SOCKET s, WsOpcode opcode, const std::string& payload);

  /// Receive a WebSocket frame from a socket. Returns the payload.
  /// Sets opcode_out to the frame's opcode. Returns empty string on error.
  std::string ws_recv_frame(SOCKET s, WsOpcode& opcode_out);

  /// TLS-aware WebSocket send (uses TlsSession for I/O when non-null).
  bool ws_send_frame(SOCKET s, WsOpcode opcode, const std::string& payload, class TlsSession* tls);

  /// TLS-aware WebSocket receive (uses TlsSession for I/O when non-null).
  std::string ws_recv_frame(SOCKET s, WsOpcode& opcode_out, class TlsSession* tls);

} // namespace wininspect
