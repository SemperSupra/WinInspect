// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/websocket.hpp"
#include "wininspect/tls.hpp"
#include <sstream>
#include <cstring>
#include <array>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace wininspect {

  // ── SHA-1 (Minimal, for WebSocket accept key) ───────────────────────────────

  // WebSocket requires SHA-1 for the accept key computation.
  // We use BCrypt on Windows.
  static std::vector<uint8_t> sha1(const std::vector<uint8_t>& data)
  {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
    DWORD hash_len = 0, result_len = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len, sizeof(hash_len), &result_len,
                      0);
    std::vector<uint8_t> hash(hash_len);
    BCryptHash(hAlg, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), hash.data(),
               (ULONG)hash.size());
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return hash;
#else
    (void)data;
    return {};
#endif
  }

  // ── Base64 Encoding ─────────────────────────────────────────────────────────

  static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "abcdefghijklmnopqrstuvwxyz0123456789+/";

  static std::string base64_encode(const std::vector<uint8_t>& data)
  {
    std::string out;
    for (size_t i = 0; i < data.size(); i += 3) {
      uint32_t b = ((uint32_t)data[i] << 16) |
                   ((i + 1 < data.size() ? (uint32_t)data[i + 1] : 0) << 8) |
                   (i + 2 < data.size() ? (uint32_t)data[i + 2] : 0);
      out += b64_table[(b >> 18) & 0x3F];
      out += b64_table[(b >> 12) & 0x3F];
      out += (i + 1 < data.size()) ? b64_table[(b >> 6) & 0x3F] : '=';
      out += (i + 2 < data.size()) ? b64_table[b & 0x3F] : '=';
    }
    return out;
  }

  // ── WebSocket Accept Key ────────────────────────────────────────────────────

  std::string ws_accept_key(const std::string& key)
  {
    // Magic GUID defined by RFC 6455
    const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = key + magic;
    std::vector<uint8_t> data(combined.begin(), combined.end());
    auto hash = sha1(data);
    return base64_encode(hash);
  }

  // ── Upgrade Response ────────────────────────────────────────────────────────

  std::string ws_upgrade_response(const std::string& key)
  {
    auto accept = ws_accept_key(key);
    std::ostringstream ss;
    ss << "HTTP/1.1 101 Switching Protocols\r\n"
       << "Upgrade: websocket\r\n"
       << "Connection: Upgrade\r\n"
       << "Sec-WebSocket-Accept: " << accept << "\r\n"
       << "\r\n";
    return ss.str();
  }

  // ── Upgrade Check ───────────────────────────────────────────────────────────

  bool ws_is_upgrade(const std::string& method, const std::string& connection_hdr,
                     const std::string& upgrade_hdr)
  {
    // Normalize headers — WebSocket check should be case-insensitive
    auto lower = [](std::string s) {
      for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
          c += 32;
      return s;
    };
    return method == "GET" && lower(connection_hdr).find("upgrade") != std::string::npos &&
           lower(upgrade_hdr).find("websocket") != std::string::npos;
  }

  // ── Frame Encoding ──────────────────────────────────────────────────────────

  std::vector<uint8_t> ws_encode_frame(WsOpcode opcode, const std::string& payload)
  {
    std::vector<uint8_t> frame;

    // FIN + opcode
    frame.push_back((uint8_t)(0x80 | (uint8_t)opcode));

    // Payload length
    if (payload.size() < 126) {
      frame.push_back((uint8_t)payload.size());
    }
    else if (payload.size() <= 0xFFFF) {
      frame.push_back(126);
      frame.push_back((uint8_t)(payload.size() >> 8));
      frame.push_back((uint8_t)(payload.size() & 0xFF));
    }
    else {
      frame.push_back(127);
      uint64_t len = payload.size();
      for (int i = 7; i >= 0; i--)
        frame.push_back((uint8_t)(len >> (i * 8)));
    }

    // Payload (unmasked — server to client)
    frame.insert(frame.end(), payload.begin(), payload.end());

    return frame;
  }

  // ── Send Frame ──────────────────────────────────────────────────────────────

  bool ws_send_frame(SOCKET s, WsOpcode opcode, const std::string& payload)
  {
    auto frame = ws_encode_frame(opcode, payload);
    return ::send(s, (const char*)frame.data(), (int)frame.size(), 0) > 0;
  }

  // ── Receive Frame ─────────────────────────────────────────────────────────

  std::string ws_recv_frame(SOCKET s, WsOpcode& opcode_out)
  {
    // Read at least 2 bytes for the frame header
    uint8_t header[2];
    int r = recv(s, (char*)header, 2, 0);
    if (r <= 0)
      return {};

    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;
    opcode_out = (WsOpcode)(header[0] & 0x0F);

    // Extended payload length
    if (payload_len == 126) {
      uint8_t ext[2];
      if (recv(s, (char*)ext, 2, 0) <= 0)
        return {};
      payload_len = ((uint64_t)ext[0] << 8) | ext[1];
    }
    else if (payload_len == 127) {
      uint8_t ext[8];
      if (recv(s, (char*)ext, 8, 0) <= 0)
        return {};
      payload_len = 0;
      for (int i = 0; i < 8; i++)
        payload_len = (payload_len << 8) | ext[i];
    }

    // Masking key (client frames are masked)
    uint8_t mask[4] = {};
    if (masked) {
      if (recv(s, (char*)mask, 4, 0) <= 0)
        return {};
    }

    // Read payload
    if (payload_len > 10 * 1024 * 1024)
      return {}; // sanity cap
    std::string payload((size_t)payload_len, '\0');
    if (payload_len > 0) {
      if (recv(s, &payload[0], (int)payload_len, 0) <= 0)
        return {};
    }

    // Unmask if needed
    if (masked) {
      for (size_t i = 0; i < (size_t)payload_len; i++)
        payload[i] ^= mask[i % 4];
    }

    return payload;
  }

  // ── TLS-aware variants ────────────────────────────────────────────────────

  bool ws_send_frame(SOCKET s, WsOpcode opcode, const std::string& payload, TlsSession* tls)
  {
    if (!tls || !tls->is_initialized())
      return ws_send_frame(s, opcode, payload);

    auto frame = ws_encode_frame(opcode, payload);
    return tls->send((uintptr_t)s, std::vector<uint8_t>(frame.begin(), frame.end()));
  }

  // Helper: TLS-aware recv that returns the number of bytes read (like ::recv)
  static int tls_recv_all(SOCKET s, TlsSession* tls, void* buf, int len)
  {
    if (!tls || !tls->is_initialized())
      return recv(s, (char*)buf, len, 0);
    std::vector<uint8_t> data;
    if (!tls->recv((uintptr_t)s, data) || data.empty())
      return 0;
    int n = (int)std::min<size_t>(data.size(), (size_t)len);
    std::memcpy(buf, data.data(), n);
    return n;
  }

  std::string ws_recv_frame(SOCKET s, WsOpcode& opcode_out, TlsSession* tls)
  {
    if (!tls || !tls->is_initialized())
      return ws_recv_frame(s, opcode_out);

    uint8_t header[2];
    if (tls_recv_all(s, tls, header, 2) <= 0)
      return {};

    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;
    opcode_out = (WsOpcode)(header[0] & 0x0F);

    if (payload_len == 126) {
      uint8_t ext[2];
      if (tls_recv_all(s, tls, ext, 2) <= 0)
        return {};
      payload_len = ((uint64_t)ext[0] << 8) | ext[1];
    }
    else if (payload_len == 127) {
      uint8_t ext[8];
      if (tls_recv_all(s, tls, ext, 8) <= 0)
        return {};
      payload_len = 0;
      for (int i = 0; i < 8; i++)
        payload_len = (payload_len << 8) | ext[i];
    }

    uint8_t mask[4] = {};
    if (masked) {
      if (tls_recv_all(s, tls, mask, 4) <= 0)
        return {};
    }

    if (payload_len > 10 * 1024 * 1024)
      return {};
    std::string payload((size_t)payload_len, '\0');
    if (payload_len > 0) {
      if (tls_recv_all(s, tls, &payload[0], (int)payload_len) <= 0)
        return {};
    }

    if (masked) {
      for (size_t i = 0; i < (size_t)payload_len; i++)
        payload[i] ^= mask[i % 4];
    }

    return payload;
  }

} // namespace wininspect
