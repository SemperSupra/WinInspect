// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/base64.hpp"
#include "tcp_server.hpp"
#include "control_manager.hpp"
#include "wininspect/core.hpp"
#include "wininspect/logger.hpp"
#include "wininspect/crypto.hpp"

#ifdef _WIN32
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <bcrypt.h>
#include <future>
#include <memory>
#include <set>

using namespace wininspect;

static std::optional<std::string> get_str(const json::Object &o, const std::string &k) {
  auto it = o.find(k); if (it != o.end() && it->second.is_str()) return it->second.as_str();
  return std::nullopt;
}
static std::optional<double> get_num(const json::Object &o, const std::string &k) {
  auto it = o.find(k); if (it != o.end() && it->second.is_num()) return it->second.as_num();
  return std::nullopt;
}

namespace wininspectd {

// ── Winsock Init ────────────────────────────────────────────────────────────

static struct WsaInit {
  WsaInit() {
    WSADATA wsd;
    ok = (WSAStartup(MAKEWORD(2, 2), &wsd) == 0);
  }
  ~WsaInit() { if (ok) WSACleanup(); }
  bool ok = false;
} wsa_init;

// ── Socket Helpers ──────────────────────────────────────────────────────────

static bool socket_read_all(SOCKET s, void *buf, uint32_t len) {
  char *p = (char *)buf;
  while (len > 0) {
    int r = recv(s, p, (int)len, 0);
    if (r <= 0) return false;
    p += r; len -= r;
  }
  return true;
}

static bool socket_write_all(SOCKET s, const void *buf, uint32_t len) {
  const char *p = (const char *)buf;
  while (len > 0) {
    int r = send(s, p, (int)len, 0);
    if (r <= 0) return false;
    p += r; len -= r;
  }
  return true;
}

static bool encrypted_send(SOCKET s, const std::string &plaintext,
                           crypto::CryptoSession &cs) {
  auto ct = cs.encrypt(plaintext);
  if (ct.empty()) return false;
  uint32_t flen = (uint32_t)ct.size();
  return socket_write_all(s, &flen, 4) &&
         socket_write_all(s, ct.data(), flen);
}

static bool encrypted_recv(SOCKET s, std::string &plaintext,
                           crypto::CryptoSession &cs) {
  uint32_t flen = 0;
  if (!socket_read_all(s, &flen, 4)) return false;
  if (flen == 0 || flen > 10 * 1024 * 1024) return false;
  std::vector<uint8_t> ct(flen);
  if (!socket_read_all(s, ct.data(), flen)) return false;
  plaintext = cs.decrypt(ct);
  return !plaintext.empty();
}

struct AuthContext {
  const std::string &keys_data;
  std::string identity;
  std::string sig_b64;
  std::vector<uint8_t> nonce;
};

static bool verify_identity(const AuthContext &ctx) {
  std::istringstream f(ctx.keys_data);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (line.find(ctx.identity) != std::string::npos) {
      return crypto::verify_ssh_sig(ctx.nonce, ctx.sig_b64, line);
    }
  }
  return false;
}

// ── Client Handler ──────────────────────────────────────────────────────────

static void handle_socket_client(SOCKET s, wininspect::ServerState *st,
                                  wininspect::IBackend *backend,
                                  std::string auth_keys, bool read_only,
                                  bool admin_logs, bool no_clipboard,
                                  wininspect::InstanceIdentity identity) {
  wininspect::CoreEngine core(backend);
  core.set_admin_logs_enabled(admin_logs);

  DWORD timeout = 5000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

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
    BCryptGenRandom(nullptr, nonce.data(), (ULONG)nonce.size(),
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    challenge["nonce"] = base64::encode(nonce);
    if (!server_pubkey.empty())
      challenge["pubkey"] = base64::encode(server_pubkey);
  }

  std::string cj = json::dumps(challenge);
  uint32_t clen = (uint32_t)cj.size();
  if (!socket_write_all(s, &clen, 4) || !socket_write_all(s, cj.data(), clen)) {
    closesocket(s); return;
  }

  // 2. Auth + Key Exchange
  if (!auth_keys.empty()) {
    uint32_t rlen = 0;
    if (!socket_read_all(s, &rlen, 4)) { closesocket(s); return; }
    std::string resp_json;
    resp_json.resize(rlen);
    if (!socket_read_all(s, resp_json.data(), rlen)) { closesocket(s); return; }

    try {
      auto v = json::parse(resp_json).as_obj();
      if (v.at("version").as_str() != PROTOCOL_VERSION) { closesocket(s); return; }
      AuthContext ctx{auth_keys, v.at("identity").as_str(), v.at("signature").as_str(), nonce};
      if (!verify_identity(ctx)) { closesocket(s); return; }
      auto it_pk = v.find("pubkey");
      if (it_pk != v.end() && it_pk->second.is_str()) {
        auto client_pk = base64::decode(it_pk->second.as_str());
        if (!crypto.compute_shared_secret(client_pk))
          LOG_DEBUG("ECDH shared secret computation failed");
      }
    } catch (...) { closesocket(s); return; }

    json::Object status;
    status["type"] = "auth_status";
    status["ok"] = true;
    std::string sj = json::dumps(status);
    uint32_t slen = (uint32_t)sj.size();
    if (!socket_write_all(s, &slen, 4) || !socket_write_all(s, sj.data(), slen)) {
      closesocket(s); return;
    }
  }

  DWORD idle_timeout = 30 * 60 * 1000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&idle_timeout, sizeof(idle_timeout));

  bool encrypted = crypto.is_initialized();

  while (true) {
    std::string json_req;
    if (encrypted) { if (!encrypted_recv(s, json_req, crypto)) break; }
    else {
      uint32_t len = 0;
      if (!socket_read_all(s, &len, 4)) break;
      if (len == 0 || len > 10 * 1024 * 1024) break;
      json_req.resize(len);
      if (!socket_read_all(s, json_req.data(), len)) break;
    }

    wininspect::CoreResponse resp;
    bool canonical = false;
    std::string pinned_sid;
    wininspect::ClientSession session;

    try {
      auto req = wininspect::parse_request_json(json_req);
      resp.id = req.id;

      auto itsid = req.params.find("session_id");
      if (itsid != req.params.end() && itsid->second.is_str()) {
        std::string sid_str = itsid->second.as_str();
        std::lock_guard<std::mutex> lk(st->snapshots_mu);
        if (st->sessions.size() >= st->max_sessions && !st->sessions.count(sid_str)) {
          resp.ok = false; resp.error_code = "E_TOO_MANY_SESSIONS";
          resp.error_message = "session limit reached"; goto send_resp;
        }
        if (st->sessions.count(sid_str)) {
          auto &ps = st->sessions[sid_str];
          session.id = SessionID(sid_str);
          session.last_snap_id = ps.last_snap_id;
          session.subscribed = ps.subscribed;
          ps.last_activity = std::chrono::steady_clock::now();
        } else {
          session.id = SessionID(sid_str);
          st->sessions[sid_str] = {"", false, std::chrono::steady_clock::now()};
        }
      }

      if (req.method == "events.subscribe") {
        Snapshot snap_base = backend->capture_snapshot();
        std::string sid;
        {
          std::lock_guard<std::mutex> lk(st->snapshots_mu);
          sid = "s-" + std::to_string(st->snap_counter++);
          st->snaps.emplace(sid, std::make_shared<Snapshot>(std::move(snap_base)));
          st->lru_order.push_back(sid);
          session.subscribed = true; session.last_snap_id = sid;
          if (!session.id.empty()) {
            st->sessions[session.id.val].subscribed = true;
            st->sessions[session.id.val].last_snap_id = sid;
          }
        }
        json::Object o; o["subscribed"] = true; o["snapshot_id"] = sid;
        resp.ok = true; resp.result = o; goto send_resp;
      }

      if (req.method == "events.unsubscribe") {
        session.subscribed = false; session.last_snap_id.clear();
        if (!session.id.empty()) {
          std::lock_guard<std::mutex> lk(st->snapshots_mu);
          if (st->sessions.count(session.id.val)) {
            st->sessions[session.id.val].subscribed = false;
            st->sessions[session.id.val].last_snap_id.clear();
          }
        }
        json::Object o; o["unsubscribed"] = true;
        resp.ok = true; resp.result = o; goto send_resp;
      }

      // ── Control awareness ──────────────────────────────────────────
      static auto s_control = std::make_unique<wininspectd::ControlManager>();
      if (req.method == "control.take") {
        auto who_s = get_str(req.params, "controller").value_or("human");
        auto who = wininspect::controller_type_from_str(who_s);
        auto id = get_str(req.params, "id").value_or("");
        auto ok = s_control->take_control(who, id);
        if (!ok) { resp.ok = false; resp.error_code = "E_CONTROL_DENIED"; resp.error_message = "cannot take control"; goto send_resp; }
        wininspect::json::Object o; o["controller"] = who_s; o["ok"] = ok;
        resp.ok = true; resp.result = o; goto send_resp;
      }
      if (req.method == "control.release") {
        auto who_s = get_str(req.params, "controller").value_or("human");
        auto who = wininspect::controller_type_from_str(who_s);
        auto id = get_str(req.params, "id").value_or("");
        s_control->release_control(who, id);
        wininspect::json::Object o; o["ok"] = true; resp.ok = true; resp.result = o; goto send_resp;
      }
      if (req.method == "control.status") { resp.ok = true; resp.result = s_control->get_status(); goto send_resp; }
      if (req.method == "control.setMode") {
        auto mode = get_str(req.params, "mode").value_or("hybrid");
        s_control->set_operation_mode(mode);
        wininspect::json::Object o; o["mode"] = mode; o["ok"] = true; resp.ok = true; resp.result = o; goto send_resp;
      }
      if (req.method == "control.auditLog") {
        auto max = (size_t)get_num(req.params, "max").value_or(100);
        resp.ok = true; resp.result = s_control->get_audit_log(max); goto send_resp;
      }

      if (req.method == "clipboard.read" && no_clipboard) {
        resp.ok = false; resp.error_code = "E_ACCESS_DENIED";
        resp.error_message = "clipboard access disabled"; goto send_resp;
      }
      if (req.method == "clipboard.write" && no_clipboard) {
        resp.ok = false; resp.error_code = "E_ACCESS_DENIED";
        resp.error_message = "clipboard access disabled"; goto send_resp;
      }

      if (!st->allow_methods.empty() && !st->allow_methods.count(req.method)) {
        resp.ok = false; resp.error_code = "E_ACCESS_DENIED";
        resp.error_message = "method not in allow list"; goto send_resp;
      }
      if (st->deny_methods.count(req.method)) {
        resp.ok = false; resp.error_code = "E_ACCESS_DENIED";
        resp.error_message = "method is denied"; goto send_resp;
      }

      if (read_only &&
          (req.method == "window.postMessage" || req.method == "input.send" ||
           req.method.find("reg.write") != std::string::npos)) {
        resp.ok = false; resp.error_code = "E_ACCESS_DENIED";
        resp.error_message = "read-only mode"; goto send_resp;
      }

      auto itc = req.params.find("canonical");
      if (itc != req.params.end() && itc->second.is_bool())
        canonical = itc->second.as_bool();

      if (req.method == "snapshot.capture") {
        Snapshot s = backend->capture_snapshot();
        std::string sid;
        {
          std::lock_guard<std::mutex> lk(st->snapshots_mu);
          sid = "s-" + std::to_string(st->snap_counter++);
          st->snaps.emplace(sid, std::make_shared<Snapshot>(std::move(s)));
          st->lru_order.push_back(sid);
          while (st->lru_order.size() > st->max_snapshots) {
            std::string oldest = st->lru_order.front();
            if (st->pinned_counts[oldest] > 0) { st->lru_order.pop_front(); st->lru_order.push_back(oldest); continue; }
            st->lru_order.pop_front(); st->snaps.erase(oldest); st->pinned_counts.erase(oldest);
          }
        }
        json::Object o; o["snapshot_id"] = sid;
        resp.ok = true; resp.result = o;
      } else {
        Snapshot snap;
        const Snapshot *old_ptr = nullptr;
        Snapshot old_storage;
        auto its = req.params.find("snapshot_id");
        if (its != req.params.end() && its->second.is_str()) {
          std::string sid = its->second.as_str();
          std::lock_guard<std::mutex> lk(st->snapshots_mu);
          auto it = st->snaps.find(sid);
          if (it == st->snaps.end()) {
            resp.ok = false; resp.error_code = "E_BAD_SNAPSHOT";
            resp.error_message = "unknown snapshot"; goto send_resp;
          }
          snap = *it->second; pinned_sid = sid; st->pinned_counts[sid]++;
        } else {
          snap = backend->capture_snapshot();
        }
        auto itos = req.params.find("old_snapshot_id");
        if (itos != req.params.end() && itos->second.is_str()) {
          std::string osid = itos->second.as_str();
          std::lock_guard<std::mutex> lk(st->snapshots_mu);
          auto it = st->snaps.find(osid);
          if (it != st->snaps.end()) { old_storage = *it->second; old_ptr = &old_storage; }
        } else if (req.method == "events.poll" && !session.last_snap_id.empty()) {
          std::lock_guard<std::mutex> lk(st->snapshots_mu);
          auto it = st->snaps.find(session.last_snap_id);
          if (it != st->snaps.end()) { old_storage = *it->second; old_ptr = &old_storage; }
        }
        auto future = std::async(std::launch::async, [&core, req, snap, old_ptr]() {
          return core.handle(req, snap, old_ptr);
        });
        if (future.wait_for(std::chrono::milliseconds(st->request_timeout_ms)) == std::future_status::timeout) {
          resp.ok = false; resp.error_code = "E_TIMEOUT";
        } else { resp = future.get(); }
        if (req.method == "events.poll" && resp.ok) {
          Snapshot fresh = backend->capture_snapshot();
          std::string sid;
          { std::lock_guard<std::mutex> lk(st->snapshots_mu);
            sid = "s-" + std::to_string(st->snap_counter++);
            st->snaps.emplace(sid, std::make_shared<Snapshot>(std::move(fresh)));
            st->lru_order.push_back(sid); session.last_snap_id = sid;
            if (!session.id.empty()) st->sessions[session.id.val].last_snap_id = sid;
          }
        }
      }
    } catch (...) { resp.ok = false; resp.error_code = "E_BAD_REQUEST"; }

  send_resp:
    std::string out = serialize_response_json(resp, canonical);
    if (encrypted) { encrypted_send(s, out, crypto); }
    else {
      uint32_t out_len = (uint32_t)out.size();
      socket_write_all(s, &out_len, 4); socket_write_all(s, out.data(), out_len);
    }
    if (!pinned_sid.empty()) {
      std::lock_guard<std::mutex> lk(st->snapshots_mu);
      st->pinned_counts[pinned_sid]--;
    }
  }
  closesocket(s);
}

// ── TcpServer Implementation ────────────────────────────────────────────────

TcpServer::TcpServer(wininspect::ServerState *state,
                     wininspect::IBackend *backend)
    : state_(state), backend_(backend) {}

TcpServer::~TcpServer() { stop(); }

void TcpServer::stop() {
  for (auto sock : listen_socks_) {
    if (sock != 0)
      closesocket((SOCKET)sock);
  }
  listen_socks_.clear();
}

void TcpServer::start(std::atomic<bool> *running,
                       const wininspect::NetworkConfig &cfg,
                       const std::string &auth_keys,
                       bool read_only, bool admin_logs,
                       bool no_clipboard) {
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

  for (auto &ba : cfg.bind) {
    struct addrinfo hints = {};
    hints.ai_family = ba.family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *result = nullptr;
    int rc = getaddrinfo(ba.address.c_str(), port_str.c_str(), &hints, &result);
    if (rc != 0) {
      LOG_WARN("TCP Server: getaddrinfo failed for " + ba.address +
               ": " + std::to_string(rc));
      continue;
    }

    for (auto *rp = result; rp; rp = rp->ai_next) {
      SOCKET s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (s == INVALID_SOCKET) {
        LOG_WARN("TCP Server: socket() failed for " + ba.address +
                 ": " + std::to_string(WSAGetLastError()));
        continue;
      }

      // Dual-stack: disable IPV6_V6ONLY so AF_INET6 handles both v4 and v6
      if (rp->ai_family == AF_INET6 && ba.family == AF_UNSPEC) {
        int off = 0;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof(off));
      }

      // Enable address reuse for quick restarts
      int on = 1;
      setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));

      if (bind(s, rp->ai_addr, (int)rp->ai_addrlen) == SOCKET_ERROR) {
        LOG_ERROR("TCP Server: bind failed on " + ba.address + ":" + port_str +
                  " — " + std::to_string(WSAGetLastError()));
        closesocket(s);
        continue;
      }

      if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("TCP Server: listen failed on " + ba.address +
                  ": " + std::to_string(WSAGetLastError()));
        closesocket(s);
        continue;
      }

      // Get the actual bound address for logging
      struct sockaddr_storage bound_addr;
      socklen_t bound_len = sizeof(bound_addr);
      char addr_str[64] = {};
      if (getsockname(s, (struct sockaddr *)&bound_addr, &bound_len) == 0) {
        if (bound_addr.ss_family == AF_INET) {
          auto *sin = (struct sockaddr_in *)&bound_addr;
          inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
        } else if (bound_addr.ss_family == AF_INET6) {
          auto *sin6 = (struct sockaddr_in6 *)&bound_addr;
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
      if (s > max_fd) max_fd = s;
    }

    struct timeval tv = {0, 100000}; // 100ms timeout
    int sel_rc = select((int)max_fd + 1, &read_fds, nullptr, nullptr, &tv);
    if (sel_rc == SOCKET_ERROR) break;
    if (sel_rc == 0) continue; // timeout, loop

    for (auto s : socks) {
      if (!FD_ISSET(s, &read_fds)) continue;

      SOCKET client = accept(s, nullptr, nullptr);
      if (client == INVALID_SOCKET) continue;

      u_long mode = 0;
      ioctlsocket(client, FIONBIO, &mode);

      // Rate limiting
      if (cfg.rate_limit_ms > 0) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state_->last_accept_time).count();
        if (elapsed < cfg.rate_limit_ms) { closesocket(client); continue; }
        state_->last_accept_time = now;
      }

      {
        std::lock_guard<std::mutex> lk(state_->thread_mu);
        state_->client_threads.emplace_back(
            [this, client, auth_keys, read_only, admin_logs, no_clipboard]() {
              handle_socket_client(client, state_, backend_, auth_keys,
                                   read_only, admin_logs, no_clipboard,
                                   backend_->get_instance_identity());
            });
      }
    }
  }

  for (auto s : socks) closesocket(s);
  listen_socks_.clear();
}

} // namespace wininspectd
#endif
