#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <thread>
#include <vector>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <wincrypt.h>
#include <future>

#include "tcp_server.hpp"
#include "wininspect/core.hpp"
#include "wininspect/logger.hpp"

#include "wininspect/crypto.hpp"

using namespace wininspect;

namespace wininspectd {

static bool socket_read_all(SOCKET s, void *buf, uint32_t len) {
  char *p = (char *)buf;
  while (len > 0) {
    int r = recv(s, p, (int)len, 0);
    if (r <= 0)
      return false;
    p += r;
    len -= r;
  }
  return true;
}

static bool socket_write_all(SOCKET s, const void *buf, uint32_t len) {
  const char *p = (const char *)buf;
  while (len > 0) {
    int r = send(s, p, (int)len, 0);
    if (r <= 0)
      return false;
    p += r;
    len -= r;
  }
  return true;
}

static std::string base64_encode(const std::vector<uint8_t> &in) {
  static const char *b64 =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, valb = -6;
  for (uint8_t c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(b64[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6)
    out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4)
    out.push_back('=');
  return out;
}

struct AuthContext {
  std::string keys_path;
  std::string identity;
  std::string sig_b64;
  std::vector<uint8_t> nonce;
};

static bool verify_identity(const AuthContext &ctx) {
  std::ifstream f(ctx.keys_path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    if (line.find(ctx.identity) != std::string::npos) {
      return wininspect::crypto::verify_ssh_sig(ctx.nonce, ctx.sig_b64, line);
    }
  }
  return false;
}

static void handle_socket_client(SOCKET s, wininspect::ServerState *st,
                                 wininspect::IBackend *backend,
                                 std::string auth_keys, bool read_only) {
  wininspect::CoreEngine core(backend);

  // Set 5 second timeout for handshake
  DWORD timeout = 5000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
             sizeof(timeout));

  // 1. Always send Hello/Challenge
  std::vector<uint8_t> nonce;
  wininspect::json::Object challenge;
  challenge["type"] = "hello";
  challenge["version"] = std::string(wininspect::PROTOCOL_VERSION);

  if (!auth_keys.empty()) {
    nonce.resize(32);
    HCRYPTPROV hProv;
    if (CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL,
                            CRYPT_VERIFYCONTEXT)) {
      CryptGenRandom(hProv, (DWORD)nonce.size(), nonce.data());
      CryptReleaseContext(hProv, 0);
    }
    challenge["nonce"] = base64_encode(nonce);
  }

  std::string cj = wininspect::json::dumps(challenge);
  uint32_t clen = (uint32_t)cj.size();
  if (!socket_write_all(s, &clen, 4) ||
      !socket_write_all(s, cj.data(), clen)) {
    closesocket(s);
    return;
  }

  // 2. Perform Auth if keys are configured
  if (!auth_keys.empty()) {
    uint32_t rlen = 0;
    if (!socket_read_all(s, &rlen, 4)) {
      closesocket(s);
      return;
    }
    std::string resp_json;
    resp_json.resize(rlen);
    if (!socket_read_all(s, resp_json.data(), rlen)) {
      closesocket(s);
      return;
    }

    try {
      auto v = wininspect::json::parse(resp_json).as_obj();
      if (v.at("version").as_str() != wininspect::PROTOCOL_VERSION) {
        closesocket(s);
        return;
      }
      AuthContext ctx{auth_keys, v.at("identity").as_str(), v.at("signature").as_str(), nonce};
      if (!verify_identity(ctx)) {
        closesocket(s);
        return;
      }
    } catch (...) {
      closesocket(s);
      return;
    }

    wininspect::json::Object status;
    status["type"] = "auth_status";
    status["ok"] = true;
    std::string sj = wininspect::json::dumps(status);
    uint32_t slen = (uint32_t)sj.size();
    if (!socket_write_all(s, &slen, 4) ||
        !socket_write_all(s, sj.data(), slen)) {
      closesocket(s);
      return;
    }
  }

  // Handshake successful, set a longer idle timeout (30 mins)
  DWORD idle_timeout = 30 * 60 * 1000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&idle_timeout,
             sizeof(idle_timeout));

  while (true) {
    uint32_t len = 0;
    if (!socket_read_all(s, &len, 4))
      break;

    // Security: Prevent OOM/DoS by enforcing a reasonable maximum message size
    // (10MB)
    if (len == 0 || len > 10 * 1024 * 1024)
      break;

    std::string json_req;
    json_req.resize(len);
    if (!socket_read_all(s, json_req.data(), len))
      break;

    wininspect::CoreResponse resp;
    bool canonical = false;
    std::string pinned_sid;
    wininspect::ClientSession session; // Temporary session for this connection

    try {
      auto req = wininspect::parse_request_json(json_req);
      resp.id = req.id;

      // Handle session_id for persistence
      auto itsid = req.params.find("session_id");
      if (itsid != req.params.end() && itsid->second.is_str()) {
        std::string sid_str = itsid->second.as_str();
        std::lock_guard<std::mutex> lk(st->mu);
        if (st->sessions.count(sid_str)) {
          auto &ps = st->sessions[sid_str];
          session.id = SessionID(sid_str);
          session.last_snap_id = ps.last_snap_id;
          session.subscribed = ps.subscribed;
          ps.last_activity = std::chrono::steady_clock::now();
        } else {
          session.id = SessionID(sid_str);
          st->sessions[sid_str] = { "", false, std::chrono::steady_clock::now() };
        }
      }

      // Security: Check Read-Only mode
      if (read_only &&
          (req.method == "window.postMessage" || req.method == "input.send" || req.method.find("reg.write") != std::string::npos)) {
        resp.ok = false;
        resp.error_code = "E_ACCESS_DENIED";
        resp.error_message = "daemon is running in read-only mode";
        goto send_resp;
      }

      auto itc = req.params.find("canonical");
      if (itc != req.params.end() && itc->second.is_bool())
        canonical = itc->second.as_bool();

      if (req.method == "snapshot.capture") {
        wininspect::Snapshot s = backend->capture_snapshot();
        std::string sid;
        {
          std::lock_guard<std::mutex> lk(st->mu);
          sid = "s-" + std::to_string(st->snap_counter++);
          st->snaps.emplace(sid, std::move(s));
          st->lru_order.push_back(sid);
          while (st->lru_order.size() > st->max_snapshots) {
            std::string oldest = st->lru_order.front();
            if (st->pinned_counts[oldest] > 0) {
              st->lru_order.pop_front();
              st->lru_order.push_back(oldest);
              continue;
            }
            st->lru_order.pop_front();
            st->snaps.erase(oldest);
            st->pinned_counts.erase(oldest);
          }
        }
        wininspect::json::Object o;
        o["snapshot_id"] = sid;
        resp.ok = true;
        resp.result = o;
      } else {
        wininspect::Snapshot snap;
        const wininspect::Snapshot *old_ptr = nullptr;
        wininspect::Snapshot old_storage;

        auto its = req.params.find("snapshot_id");
        if (its != req.params.end() && its->second.is_str()) {
          std::string sid = its->second.as_str();
          std::lock_guard<std::mutex> lk(st->mu);
          auto it = st->snaps.find(sid);
          if (it == st->snaps.end()) {
            resp.ok = false;
            resp.error_code = "E_BAD_SNAPSHOT";
            resp.error_message = "unknown snapshot";
            goto send_resp;
          }
          snap = it->second;
          pinned_sid = sid;
          st->pinned_counts[sid]++;
        } else {
          snap = backend->capture_snapshot();
        }

        auto itos = req.params.find("old_snapshot_id");
        if (itos != req.params.end() && itos->second.is_str()) {
          std::string osid = itos->second.as_str();
          std::lock_guard<std::mutex> lk(st->mu);
          auto it = st->snaps.find(osid);
          if (it != st->snaps.end()) {
            old_storage = it->second;
            old_ptr = &old_storage;
          }
        } else if (req.method == "events.poll" && !session.last_snap_id.empty()) {
          std::lock_guard<std::mutex> lk(st->mu);
          auto it = st->snaps.find(session.last_snap_id);
          if (it != st->snaps.end()) {
            old_storage = it->second;
            old_ptr = &old_storage;
          }
        }

        // Watchdog: run core in async task
        auto future = std::async(std::launch::async, [&]() {
          return core.handle(req, snap, old_ptr);
        });

        if (future.wait_for(std::chrono::milliseconds(st->request_timeout_ms)) == std::future_status::timeout) {
          resp.ok = false;
          resp.error_code = "E_TIMEOUT";
        } else {
          resp = future.get();
        }

        if (req.method == "events.poll" && resp.ok) {
          wininspect::Snapshot fresh = backend->capture_snapshot();
          std::string sid;
          {
            std::lock_guard<std::mutex> lk(st->mu);
            sid = "s-" + std::to_string(st->snap_counter++);
            st->snaps.emplace(sid, fresh);
            st->lru_order.push_back(sid);
            session.last_snap_id = sid;
            if (!session.id.empty()) {
              st->sessions[session.id.val].last_snap_id = sid;
            }
          }
        }
      }
    } catch (...) {
      resp.ok = false;
      resp.error_code = "E_BAD_REQUEST";
    }

  send_resp:
    std::string out = wininspect::serialize_response_json(resp, canonical);
    uint32_t out_len = (uint32_t)out.size();
    socket_write_all(s, &out_len, 4);
    socket_write_all(s, out.data(), out_len);

    if (!pinned_sid.empty()) {
      std::lock_guard<std::mutex> lk(st->mu);
      st->pinned_counts[pinned_sid]--;
    }
  }
  closesocket(s);
}

TcpServer::TcpServer(int port, wininspect::ServerState *state,
                     wininspect::IBackend *backend)
    : port_(port), state_(state), backend_(backend) {}

TcpServer::~TcpServer() {}

void TcpServer::start(std::atomic<bool> *running, bool bind_public,
                      const std::string &auth_keys, bool read_only) {
  LOG_DEBUG("TCP Server: Initializing Winsock...");
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    LOG_ERROR("TCP Server: WSAStartup failed.");
    return;
  }

  LOG_DEBUG("TCP Server: Creating socket...");
  SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_sock == INVALID_SOCKET) {
    LOG_ERROR("TCP Server: socket() failed: " + std::to_string(WSAGetLastError()));
    WSACleanup();
    return;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(bind_public ? INADDR_ANY : INADDR_LOOPBACK);
  addr.sin_port = htons((u_short)port_);

  LOG_DEBUG("TCP Server: Binding to " + std::string(bind_public ? "0.0.0.0" : "127.0.0.1") + ":" + std::to_string(port_) + "...");
  if (bind(listen_sock, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    LOG_ERROR("TCP Server: Bind failed: " + std::to_string(WSAGetLastError()));
    closesocket(listen_sock);
    WSACleanup();
    return;
  }

  LOG_INFO("TCP Server listening on " + std::string(bind_public ? "0.0.0.0" : "127.0.0.1") + ":" + std::to_string(port_));

  if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
    closesocket(listen_sock);
    return;
  }

  // Set non-blocking to check 'running' flag
  u_long mode = 1;
  ioctlsocket(listen_sock, FIONBIO, &mode);

  while (running->load()) {
    SOCKET client = accept(listen_sock, nullptr, nullptr);
    if (client == INVALID_SOCKET) {
      if (WSAGetLastError() == WSAEWOULDBLOCK) {
        Sleep(100);
        continue;
      }
      break;
    }

    // Set back to blocking for the handler thread
    u_long m2 = 0;
    ioctlsocket(client, FIONBIO, &m2);

    std::thread(handle_socket_client, client, state_, backend_, auth_keys,
                read_only)
        .detach();
  }

  closesocket(listen_sock);
  WSACleanup();
}

} // namespace wininspectd
#endif
