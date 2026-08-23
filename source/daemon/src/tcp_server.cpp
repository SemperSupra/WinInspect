// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/base64.hpp"
#include "tcp_server.hpp"
#include "control_manager.hpp"
#include "wininspect/core.hpp"
#include "wininspect/logger.hpp"
#include "wininspect/crypto.hpp"
#include "wininspect/tls.hpp"
#include "wininspect/compress.hpp"
#include "request_handler.hpp"

#ifdef _WIN32
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h> // tcp_keepalive, SIO_KEEPALIVE_VALS
#include <fstream>
#include <bcrypt.h>
#include <future>
#include <memory>
#include <set>

using namespace wininspect;

namespace wininspectd {

  // ── Winsock Init ────────────────────────────────────────────────────────────

  static struct WsaInit
  {
    WsaInit()
    {
      WSADATA wsd;
      ok = (WSAStartup(MAKEWORD(2, 2), &wsd) == 0);
    }
    ~WsaInit()
    {
      if (ok)
        WSACleanup();
    }
    bool ok = false;
  } wsa_init;

  // ── Socket Helpers ──────────────────────────────────────────────────────────

  // TLS-aware read: uses SSL_read when tls is non-null, recv otherwise.
  static bool socket_read_all(SOCKET s, void* buf, uint32_t len,
                              wininspect::TlsSession* tls = nullptr)
  {
    char* p = (char*)buf;
    if (tls && tls->is_initialized()) {
      while (len > 0) {
        std::vector<uint8_t> chunk;
        if (!tls->recv((uintptr_t)s, chunk) || chunk.empty())
          return false;
        uint32_t avail = (uint32_t)std::min<size_t>(chunk.size(), len);
        memcpy(p, chunk.data(), avail);
        p += avail;
        len -= avail;
      }
      return true;
    }
    while (len > 0) {
      int r = recv(s, p, (int)len, 0);
      if (r <= 0)
        return false;
      p += r;
      len -= r;
    }
    return true;
  }

  // Sets TCP keepalive on a connected socket so dead peers are detected
  // before the 30-minute idle timeout. Uses 5s idle, 1s interval, 3 probes.
  static bool set_tcp_keepalive(SOCKET s)
  {
    // Enable SO_KEEPALIVE first
    char optval = 1;
    if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) != 0)
      return false;

    // Set keepalive parameters via WSAIoctl (Windows-specific)
    // onoff=1, keepalivetime=5000ms, keepaliveinterval=1000ms
    tcp_keepalive ka = {1, 5000, 1000};
    DWORD dwBytes = 0;
    if (WSAIoctl(s, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), nullptr, 0, &dwBytes, nullptr, nullptr) !=
        0)
      return false;
    return true;
  }

  // TLS-aware write: uses SSL_write when tls is non-null, send otherwise.
  static bool socket_write_all(SOCKET s, const void* buf, uint32_t len,
                               wininspect::TlsSession* tls = nullptr)
  {
    const char* p = (const char*)buf;
    if (tls && tls->is_initialized()) {
      std::vector<uint8_t> data(p, p + len);
      return tls->send((uintptr_t)s, data);
    }
    while (len > 0) {
      int r = send(s, p, (int)len, 0);
      if (r <= 0)
        return false;
      p += r;
      len -= r;
    }
    return true;
  }

  static bool encrypted_send(SOCKET s, const std::string& plaintext, crypto::CryptoSession& cs)
  {
    auto ct = cs.encrypt(plaintext);
    if (ct.empty())
      return false;
    uint32_t flen = (uint32_t)ct.size();
    uint32_t flen_net = htonl(flen);
    return socket_write_all(s, &flen_net, 4) && socket_write_all(s, ct.data(), flen);
  }

  static bool encrypted_recv(SOCKET s, std::string& plaintext, crypto::CryptoSession& cs)
  {
    uint32_t flen = 0;
    uint32_t flen_net;
    if (!socket_read_all(s, &flen_net, 4))
      return false;
    flen = ntohl(flen_net);
    if (flen == 0 || flen > 10 * 1024 * 1024)
      return false;
    std::vector<uint8_t> ct(flen);
    if (!socket_read_all(s, ct.data(), flen))
      return false;
    plaintext = cs.decrypt(ct);
    return !plaintext.empty();
  }

  struct AuthContext
  {
    const std::string& keys_data;
    std::string identity;
    std::string sig_b64;
    std::vector<uint8_t> nonce;
  };

  static bool verify_identity(const AuthContext& ctx)
  {
    std::istringstream f(ctx.keys_data);
    std::string line;
    while (std::getline(f, line)) {
      if (line.empty() || line[0] == '#')
        continue;
      if (line.find(ctx.identity) != std::string::npos) {
        return crypto::verify_ssh_sig(ctx.nonce, ctx.sig_b64, line);
      }
    }
    return false;
  }

  // ── Client Handler ──────────────────────────────────────────────────────────

  static void handle_socket_client(SOCKET s, wininspect::ServerState* st,
                                   wininspect::IBackend* backend, std::string auth_keys,
                                   bool read_only, bool admin_logs, bool no_clipboard,
                                   wininspect::InstanceIdentity identity,
                                   int idle_timeout_ms = 1800000, bool audit_all = false,
                                   wininspect::TlsSession* tls = nullptr)
  {
    wininspect::CoreEngine core(backend);
    core.set_admin_logs_enabled(admin_logs);
    core.set_read_only(read_only);
    if (audit_all && st->control) {
      core.set_audit_hook([st](const CoreRequest& req, const CoreResponse& resp) {
        st->control->log_action(req.method, req.params, resp.ok, 0);
      });
    }

    // Use idle timeout from the start (Wine needs more time for BCrypt init)
    DWORD timeout = (DWORD)idle_timeout_ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    crypto::CryptoSession crypto;
    auto server_pubkey = crypto.generate_local_key();
    std::vector<uint8_t> nonce;

    // 1. Hello/Challenge with identity + ECDH
    json::Object challenge;
    challenge["type"] = "hello";
    challenge["version"] = std::string(PROTOCOL_VERSION);
    challenge["uuid"] = identity.uuid;
    challenge["name"] = identity.name;
    challenge["hostname"] = identity.hostname;
    if (!identity.ecdh_pubkey.empty())
      challenge["server_pubkey"] = identity.ecdh_pubkey;

    if (!auth_keys.empty()) {
      nonce.resize(32);
      BCryptGenRandom(nullptr, nonce.data(), (ULONG)nonce.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
      challenge["nonce"] = base64::encode(nonce);
      if (!server_pubkey.empty())
        challenge["pubkey"] = base64::encode(server_pubkey);
    }

    std::string cj = json::dumps(challenge);
    uint32_t clen = (uint32_t)cj.size();
    uint32_t clen_net = htonl(clen);
    if (!socket_write_all(s, &clen_net, 4, tls) || !socket_write_all(s, cj.data(), clen, tls)) {
      closesocket(s);
      return;
    }

    // 2. Auth + Key Exchange
    if (!auth_keys.empty()) {
      uint32_t rlen = 0;
      uint32_t rlen_net;
      if (!socket_read_all(s, &rlen_net, 4, tls)) {
        closesocket(s);
        return;
      }
      rlen = ntohl(rlen_net);
      std::string resp_json;
      resp_json.resize(rlen);
      if (!socket_read_all(s, resp_json.data(), rlen, tls)) {
        closesocket(s);
        return;
      }

      try {
        auto v = json::parse(resp_json).as_obj();
        if (v.at("version").as_str() != PROTOCOL_VERSION) {
          closesocket(s);
          return;
        }
        AuthContext ctx{auth_keys, v.at("identity").as_str(), v.at("signature").as_str(), nonce};
        if (!verify_identity(ctx)) {
          closesocket(s);
          return;
        }
        auto it_pk = v.find("pubkey");
        if (it_pk != v.end() && it_pk->second.is_str()) {
          auto client_pk = base64::decode(it_pk->second.as_str());
          if (!crypto.compute_shared_secret(client_pk))
            LOG_DEBUG("ECDH shared secret computation failed");
        }
      }
      catch (...) {
        closesocket(s);
        return;
      }

      json::Object status;
      status["type"] = "auth_status";
      status["ok"] = true;
      std::string sj = json::dumps(status);
      uint32_t slen = (uint32_t)sj.size();
      uint32_t slen_net = htonl(slen);
      if (!socket_write_all(s, &slen_net, 4, tls) || !socket_write_all(s, sj.data(), slen, tls)) {
        closesocket(s);
        return;
      }
    }

    DWORD idle_timeout = (DWORD)idle_timeout_ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&idle_timeout, sizeof(idle_timeout));

    // When TLS is active, skip per-frame AES-GCM encryption (TLS handles transport security)
    bool encrypted = tls && tls->is_initialized() ? false : crypto.is_initialized();
    bool compress_responses = false;
    bool version_checked = false;
    wininspect::ClientSession session; // persists across requests in one connection
    session.authenticated = true;      // TCP auth done via handshake

    while (true) {
      std::string json_req;
      if (encrypted) {
        if (!encrypted_recv(s, json_req, crypto))
          break;
      }
      else {
        uint32_t len = 0;
        uint32_t frame_len_net;
        if (!socket_read_all(s, &frame_len_net, 4, tls))
          break;
        len = ntohl(frame_len_net);

        // Check for compression flag in length field
        bool is_compressed = false;
        if (len & wininspect::FRAME_COMPRESSED_FLAG) {
          is_compressed = true;
          len &= ~wininspect::FRAME_COMPRESSED_FLAG;
          // Client shouldn't send compressed requests (requests are small)
          // But handle it gracefully by reading the compressed payload
        }

        if (len == 0 || len > 10 * 1024 * 1024)
          break;
        json_req.resize(len);
        if (!socket_read_all(s, json_req.data(), len, tls))
          break;

        // Decompress request if needed (clients may compress large requests)
        if (is_compressed) {
          auto decomp =
              wininspect::decompress(std::vector<uint8_t>(json_req.begin(), json_req.end()), len);
          if (decomp.empty())
            break;
          json_req.assign(decomp.begin(), decomp.end());
        }
      }
      // -- Process request via shared handler --------------------------
      // All protocol logic (session, snapshot, events, control, dispatch)
      // is in request_handler.hpp to eliminate duplication with pipe.
      wininspect::CoreResponse resp;
      bool canonical = false;
      wininspect::PinGuard pin_guard;
      bool close_connection = false;

      if (!process_request(json_req, core, st, backend, session, read_only, no_clipboard,
                           false, // TCP auth done via handshake
                           auth_keys, version_checked, compress_responses, resp, canonical,
                           pin_guard, close_connection)) {
        break; // Parse error � close connection
      }
      if (close_connection)
        break;

      // -- Prepare and send response ----------------------------------
      std::string raw_out;
      bool was_compressed = false;
      prepare_response(resp, canonical, st->max_response_size, encrypted, compress_responses,
                       raw_out, was_compressed);

      if (was_compressed) {
        // prepare_response already packed compressed framing
        if (!socket_write_all(s, raw_out.data(), (uint32_t)raw_out.size(), tls))
          break;
      }
      else if (encrypted) {
        if (!encrypted_send(s, raw_out, crypto))
          break;
      }
      else {
        uint32_t out_len = (uint32_t)raw_out.size();
        uint32_t out_len_net = htonl(out_len);
        if (!socket_write_all(s, &out_len_net, 4, tls) ||
            !socket_write_all(s, raw_out.data(), out_len, tls))
          break;
      }
      // PinGuard RAII handles unpin automatically at end of iteration
    }
    closesocket(s);
  }

  // ── TcpServer Implementation ────────────────────────────────────────────────

  TcpServer::TcpServer(wininspect::ServerState* state, wininspect::IBackend* backend)
      : state_(state), backend_(backend)
  {
  }

  TcpServer::~TcpServer()
  {
    stop();
  }

  void TcpServer::stop()
  {
    for (auto sock : listen_socks_) {
      if (sock != 0)
        closesocket((SOCKET)sock);
    }
    listen_socks_.clear();
  }

  void TcpServer::start_tls(std::atomic<bool>* running, const wininspect::NetworkConfig& cfg,
                            const std::string& cert_pem, const std::string& key_pem,
                            const std::string& auth_keys, bool read_only, bool admin_logs,
                            bool no_clipboard)
  {
    if (cfg.tls_port <= 0)
      return;
    if (!wsa_init.ok) {
      LOG_ERROR("TLS TCP: Winsock not initialized.");
      return;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
      LOG_ERROR("TLS TCP: socket() failed");
      return;
    }

    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)cfg.tls_port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
      LOG_ERROR("TLS TCP: bind failed on port " + std::to_string(cfg.tls_port));
      closesocket(s);
      return;
    }
    listen(s, SOMAXCONN);
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
    listen_socks_.push_back((uintptr_t)s);
    LOG_INFO("TLS TCP server listening on port " + std::to_string(cfg.tls_port));

    while (running->load()) {
      SOCKET client = accept(s, nullptr, nullptr);
      if (client == INVALID_SOCKET) {
        Sleep(100);
        continue;
      }

      set_tcp_keepalive(client);

      // TLS handshake on the accepted socket
      auto tls = std::make_shared<wininspect::TlsSession>();
      if (!tls->init_server(cert_pem, key_pem)) {
        LOG_ERROR("TLS TCP: init_server failed");
        closesocket(client);
        continue;
      }
      if (!tls->handshake((uintptr_t)client)) {
        closesocket(client);
        continue;
      }

      // Spawn client handler thread with TlsSession
      {
        std::lock_guard<std::mutex> lk(state_->thread_mu);
        ThreadHandle th;
        th.t = std::thread(
            [this, client, auth_keys, read_only, admin_logs, no_clipboard, tls, done = th.done]() {
              handle_socket_client(client, state_, backend_, auth_keys, read_only, admin_logs,
                                   no_clipboard, backend_->get_instance_identity(), 1800000, false,
                                   tls.get());
              *done = true;
            });
        state_->client_threads.push_back(std::move(th));
      }
    }
  }

  void TcpServer::start(std::atomic<bool>* running, const wininspect::NetworkConfig& cfg,
                        const std::string& auth_keys, bool read_only, bool admin_logs,
                        bool no_clipboard)
  {
    if (!wsa_init.ok) {
      LOG_ERROR("TCP Server: Winsock not initialized.");
      return;
    }

    if (cfg.bind.empty()) {
      LOG_ERROR("TCP Server: No bind addresses configured.");
      return;
    }

    std::string port_str = std::to_string(cfg.port);
    std::vector<SOCKET> socks;

    for (auto& ba : cfg.bind) {
      struct addrinfo hints = {};
      hints.ai_family = ba.family;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_protocol = IPPROTO_TCP;
      hints.ai_flags = AI_PASSIVE;

      struct addrinfo* result = nullptr;
      int rc = getaddrinfo(ba.address.c_str(), port_str.c_str(), &hints, &result);
      if (rc != 0) {
        LOG_WARN("TCP Server: getaddrinfo failed for " + ba.address + ": " + std::to_string(rc));
        continue;
      }

      for (auto* rp = result; rp; rp = rp->ai_next) {
        SOCKET s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s == INVALID_SOCKET) {
          LOG_WARN("TCP Server: socket() failed for " + ba.address + ": " +
                   std::to_string(WSAGetLastError()));
          continue;
        }

        // Dual-stack: disable IPV6_V6ONLY so AF_INET6 handles both v4 and v6
        if (rp->ai_family == AF_INET6 && ba.family == AF_UNSPEC) {
          int off = 0;
          setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof(off));
        }

        // Enable address reuse for quick restarts
        int on = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));

        if (bind(s, rp->ai_addr, (int)rp->ai_addrlen) == SOCKET_ERROR) {
          LOG_ERROR("TCP Server: bind failed on " + ba.address + ":" + port_str + " — " +
                    std::to_string(WSAGetLastError()));
          closesocket(s);
          continue;
        }

        if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
          LOG_ERROR("TCP Server: listen failed on " + ba.address + ": " +
                    std::to_string(WSAGetLastError()));
          closesocket(s);
          continue;
        }

        // Get the actual bound address for logging
        struct sockaddr_storage bound_addr;
        socklen_t bound_len = sizeof(bound_addr);
        char addr_str[64] = {};
        if (getsockname(s, (struct sockaddr*)&bound_addr, &bound_len) == 0) {
          if (bound_addr.ss_family == AF_INET) {
            auto* sin = (struct sockaddr_in*)&bound_addr;
            inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
          }
          else if (bound_addr.ss_family == AF_INET6) {
            auto* sin6 = (struct sockaddr_in6*)&bound_addr;
            inet_ntop(AF_INET6, &sin6->sin6_addr, addr_str, sizeof(addr_str));
          }
        }

        LOG_INFO("TCP Server listening on [" + std::string(addr_str) + "]:" + port_str);
        socks.push_back(s);
      }
      freeaddrinfo(result);
    }

    if (socks.empty()) {
      LOG_ERROR("TCP Server: No sockets could be bound.");
      return;
    }

    // Store for stop()
    for (auto s : socks)
      listen_socks_.push_back((uintptr_t)s);

    // Accept loop — use select() on all listening sockets
    fd_set read_fds;
    while (running->load()) {
      FD_ZERO(&read_fds);
      SOCKET max_fd = 0;
      for (auto s : socks) {
        FD_SET(s, &read_fds);
        if (s > max_fd)
          max_fd = s;
      }

      struct timeval tv = {0, 100000}; // 100ms timeout
      int sel_rc = select((int)max_fd + 1, &read_fds, nullptr, nullptr, &tv);
      if (sel_rc == SOCKET_ERROR)
        break;
      if (sel_rc == 0)
        continue; // timeout, loop

      for (auto s : socks) {
        if (!FD_ISSET(s, &read_fds))
          continue;

        SOCKET client = accept(s, nullptr, nullptr);
        if (client == INVALID_SOCKET)
          continue;

        set_tcp_keepalive(client);

        u_long mode = 0;
        ioctlsocket(client, FIONBIO, &mode);

        // Rate limiting
        if (cfg.rate_limit_ms > 0) {
          auto now = std::chrono::steady_clock::now();
          auto elapsed =
              std::chrono::duration_cast<std::chrono::milliseconds>(now - state_->last_accept_time)
                  .count();
          if (elapsed < cfg.rate_limit_ms) {
            closesocket(client);
            continue;
          }
          state_->last_accept_time = now;
        }

        {
          std::lock_guard<std::mutex> lk(state_->thread_mu);
          ThreadHandle th;
          th.t = std::thread([this, client, auth_keys, read_only, admin_logs, no_clipboard,
                              done = th.done, cfg]() {
            handle_socket_client(client, state_, backend_, auth_keys, read_only, admin_logs,
                                 no_clipboard, backend_->get_instance_identity(),
                                 cfg.tcp_idle_timeout_ms);
            *done = true;
          });
          state_->client_threads.push_back(std::move(th));
        }
      }
    }

    for (auto s : socks)
      closesocket(s);
    listen_socks_.clear();
  }

} // namespace wininspectd
#endif
