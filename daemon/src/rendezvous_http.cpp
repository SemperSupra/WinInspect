// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "rendezvous_client.hpp"
#include "wininspect/network_config.hpp"
#include "wininspect/base64.hpp"
#include "wininspect/logger.hpp"
#include "wininspect/crypto.hpp"
#include "wininspect/tinyjson.hpp"

using namespace wininspect;

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <sstream>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>

namespace wininspectd {

// ── HMAC-SHA256 ─────────────────────────────────────────────────────────────

/// Simple HMAC-SHA256 using BCrypt (Windows).
/// Returns empty vector on failure.
static std::vector<uint8_t> hmac_sha256(const std::vector<uint8_t> &key,
                                          const std::vector<uint8_t> &data) {
#ifdef _WIN32
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr,
                               BCRYPT_ALG_HANDLE_HMAC_FLAG);

  DWORD result_len = 0;
  DWORD hash_len = 0;
  BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len,
                    sizeof(hash_len), &result_len, 0);

  std::vector<uint8_t> hmac(hash_len);
  BCryptHash(hAlg, (PUCHAR)key.data(), (ULONG)key.size(),
             (PUCHAR)data.data(), (ULONG)data.size(),
             hmac.data(), (ULONG)hmac.size());
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return hmac;
#else
  // Stub for non-Windows (Wine/Linux — will add OpenSSL later)
  (void)key; (void)data;
  return {};
#endif
}

// ── Simple HTTP Client (inline, no external dependency) ──────────────────────

/// Minimal HTTP/S client for rendezvous communication.
/// Uses Winsock for TCP; no TLS — rendezvous server should be local/LAN.
/// For production, consider adding TLS or running behind a reverse proxy.
struct HttpResponse {
  int status_code{};
  std::string body;
};

static HttpResponse http_request(const std::string &method,
                                  const std::string &url,
                                  const std::string &content_type,
                                  const std::string &body,
                                  const std::string &auth_header) {
  HttpResponse resp;

  // Parse URL: scheme://host:port/path
  std::string host, path = "/";
  int port = 80;

  size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos) {
    LOG_ERROR("Invalid rendezvous URL: " + url);
    return resp;
  }
  std::string scheme = url.substr(0, scheme_end);
  if (scheme == "https") port = 443;  // note: no TLS, just TCP
  size_t host_start = scheme_end + 3;
  size_t path_start = url.find('/', host_start);
  std::string host_port;
  if (path_start == std::string::npos) {
    host_port = url.substr(host_start);
  } else {
    host_port = url.substr(host_start, path_start - host_start);
    path = url.substr(path_start);
  }
  size_t colon = host_port.find(':');
  if (colon != std::string::npos) {
    host = host_port.substr(0, colon);
    port = std::stoi(host_port.substr(colon + 1));
  } else {
    host = host_port;
  }

  // Create socket
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return resp;

  struct hostent *he = gethostbyname(host.c_str());
  if (!he) { closesocket(s); return resp; }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons((u_short)port);
  std::memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

  if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    closesocket(s);
    return resp;
  }

  // Build HTTP request
  std::ostringstream req;
  req << method << " " << path << " HTTP/1.1\r\n"
      << "Host: " << host << "\r\n"
      << "User-Agent: WinInspectRendezvous/0.1\r\n"
      << "Connection: close\r\n";
  if (!auth_header.empty())
    req << "Authorization: " << auth_header << "\r\n";
  if (!content_type.empty())
    req << "Content-Type: " << content_type << "\r\n";
  if (!body.empty())
    req << "Content-Length: " << body.size() << "\r\n";
  req << "\r\n";
  if (!body.empty())
    req << body;

  std::string req_str = req.str();
  send(s, req_str.data(), (int)req_str.size(), 0);

  // Read response
  char buf[4096];
  std::string raw;
  int r;
  while ((r = recv(s, buf, sizeof(buf) - 1, 0)) > 0) {
    buf[r] = '\0';
    raw += buf;
  }
  closesocket(s);

  // Parse HTTP response
  size_t hdr_end = raw.find("\r\n\r\n");
  if (hdr_end == std::string::npos) return resp;

  std::string status_line = raw.substr(0, raw.find('\r'));
  // "HTTP/1.1 200 OK"
  auto sp1 = status_line.find(' ');
  auto sp2 = status_line.find(' ', sp1 + 1);
  if (sp1 != std::string::npos && sp2 != std::string::npos)
    resp.status_code = std::stoi(status_line.substr(sp1 + 1, sp2 - sp1 - 1));

  resp.body = raw.substr(hdr_end + 4);
  return resp;
}

// ── Rendezvous HTTP Implementation ──────────────────────────────────────────

class RendezvousHttp : public IRendezvousClient {
public:
  explicit RendezvousHttp(const wininspect::RendezvousConfig &cfg)
      : cfg_(cfg) {
    // Decode crypto key if set
    if (!cfg.crypto_key.empty()) {
      key_ = base64::decode(cfg.crypto_key);
    }
    if (key_.empty()) {
      // Generate a random key on first run (for ad-hoc use)
      key_.resize(32);
#ifdef _WIN32
      BCryptGenRandom(nullptr, key_.data(), (ULONG)key_.size(),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#endif
    }
  }

  bool register_instance(const wininspect::InstanceIdentity &id,
                          const std::string &host, int port) override {
    host_ = host;
    port_ = port;
    uuid_ = id.uuid;

    wininspect::json::Object body;
    body["uuid"] = id.uuid;
    body["name"] = id.name;
    body["host"] = host;
    body["port"] = (double)port;
    body["pubkey"] = id.ecdh_pubkey;

    auto auth = make_auth("register:" + id.uuid);
    auto resp = http_request("POST", cfg_.url + "/register",
                              "application/json",
                              wininspect::json::dumps(body), auth);
    if (resp.status_code == 200 || resp.status_code == 201) {
      LOG_INFO("Registered with rendezvous server at " + cfg_.url);
      return true;
    }
    LOG_WARN("Rendezvous registration failed: HTTP " + std::to_string(resp.status_code));
    return false;
  }

  bool heartbeat() override {
    auto auth = make_auth("heartbeat:" + uuid_);
    auto resp = http_request("PUT", cfg_.url + "/heartbeat/" + uuid_,
                              "", "", auth);
    if (resp.status_code == 200) return true;
    LOG_DEBUG("Rendezvous heartbeat failed: HTTP " + std::to_string(resp.status_code));
    return false;
  }

  bool deregister() override {
    auto auth = make_auth("deregister:" + uuid_);
    auto resp = http_request("DELETE", cfg_.url + "/instances/" + uuid_,
                              "", "", auth);
    return (resp.status_code == 200 || resp.status_code == 204);
  }

  std::vector<DiscoveredInstance> discover() override {
    std::vector<DiscoveredInstance> results;
    auto auth = make_auth("discover");
    auto resp = http_request("GET", cfg_.url + "/instances",
                              "", "", auth);
    if (resp.status_code != 200) return results;

    try {
      auto v = wininspect::json::parse(resp.body);
      if (!v.is_arr()) return results;
      for (auto &item : v.as_arr()) {
        if (!item.is_obj()) continue;
        auto obj = item.as_obj();
        DiscoveredInstance di;
        auto it = obj.find("uuid");
        if (it != obj.end() && it->second.is_str()) di.uuid = it->second.as_str();
        it = obj.find("name");
        if (it != obj.end() && it->second.is_str()) di.name = it->second.as_str();
        it = obj.find("host");
        if (it != obj.end() && it->second.is_str()) di.host = it->second.as_str();
        it = obj.find("port");
        if (it != obj.end() && it->second.is_num()) di.port = (int)it->second.as_num();
        it = obj.find("pubkey");
        if (it != obj.end() && it->second.is_str()) di.pubkey = it->second.as_str();
        it = obj.find("last_seen");
        if (it != obj.end() && it->second.is_num()) di.last_seen = (int64_t)it->second.as_num();
        results.push_back(std::move(di));
      }
    } catch (...) {
      LOG_WARN("Failed to parse rendezvous discovery response");
    }
    return results;
  }

private:
  /// Generate HMAC-based auth header value.
  std::string make_auth(const std::string &context) {
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(
                  now.time_since_epoch()).count();

    std::string msg = context + ":" + std::to_string(ts);
    std::vector<uint8_t> data(msg.begin(), msg.end());
    auto h = hmac_sha256(key_, data);
    return "Rendezvous " + base64::encode(h);
  }

  wininspect::RendezvousConfig cfg_;
  std::vector<uint8_t> key_;
  std::string host_;
  int port_{};
  std::string uuid_;
};

// ── Factory ─────────────────────────────────────────────────────────────────

std::unique_ptr<IRendezvousClient> create_rendezvous_client(
    const wininspect::RendezvousConfig &cfg) {
  if (cfg.url.empty()) return nullptr;
  return std::make_unique<RendezvousHttp>(cfg);
}

} // namespace wininspectd
