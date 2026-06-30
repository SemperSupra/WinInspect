// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/logger.hpp"
#include "wininspect/base64.hpp"
#include "wininspect/tinyjson.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <chrono>

namespace wininspectd {

// ── HMAC-SHA256 ─────────────────────────────────────────────────────────────

static std::vector<uint8_t> hmac_sha256(const std::vector<uint8_t> &key,
                                          const std::vector<uint8_t> &data) {
#ifdef _WIN32
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr,
                               BCRYPT_ALG_HANDLE_HMAC_FLAG);
  DWORD hash_len = 0;
  DWORD result_len = 0;
  BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len,
                    sizeof(hash_len), &result_len, 0);
  std::vector<uint8_t> hmac(hash_len);
  BCryptHash(hAlg, (PUCHAR)key.data(), (ULONG)key.size(),
             (PUCHAR)data.data(), (ULONG)data.size(),
             hmac.data(), (ULONG)hmac.size());
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return hmac;
#else
  (void)key; (void)data; return {};
#endif
}

// ── Simple HTTP Client ──────────────────────────────────────────────────────

struct RendezvousResponse {
  int status_code = 0;
  std::string body;
};

static RendezvousResponse http_request(const std::string &method,
                                        const std::string &url,
                                        const std::string &content_type,
                                        const std::string &body,
                                        const std::string &auth_header) {
  RendezvousResponse resp;
  std::string host;
  std::string path = "/";
  int port = 80;

  size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos) return resp;
  size_t host_start = scheme_end + 3;
  size_t path_start = url.find('/', host_start);
  std::string host_port;
  if (path_start == std::string::npos) host_port = url.substr(host_start);
  else { host_port = url.substr(host_start, path_start - host_start); path = url.substr(path_start); }
  size_t colon = host_port.find(':');
  if (colon != std::string::npos) { host = host_port.substr(0, colon); port = std::stoi(host_port.substr(colon + 1)); }
  else host = host_port;

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return resp;
  struct hostent *he = gethostbyname(host.c_str());
  if (!he) { closesocket(s); return resp; }
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET; addr.sin_port = htons((u_short)port);
  std::memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
  if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) { closesocket(s); return resp; }

  std::ostringstream req;
  req << method << " " << path << " HTTP/1.1\r\nHost: " << host << "\r\nConnection: close\r\n";
  if (!auth_header.empty()) req << "Authorization: " << auth_header << "\r\n";
  if (!content_type.empty()) req << "Content-Type: " << content_type << "\r\n";
  if (!body.empty()) req << "Content-Length: " << body.size() << "\r\n";
  req << "\r\n" << body;

  std::string req_str = req.str();
  send(s, req_str.data(), (int)req_str.size(), 0);

  char buf[4096];
  std::string raw;
  int r;
  while ((r = recv(s, buf, sizeof(buf) - 1, 0)) > 0) { buf[r] = '\0'; raw += buf; }
  closesocket(s);

  auto hdr_end = raw.find("\r\n\r\n");
  if (hdr_end == std::string::npos) return resp;
  auto status_line = raw.substr(0, raw.find('\r'));
  auto sp1 = status_line.find(' ');
  auto sp2 = status_line.find(' ', sp1 + 1);
  if (sp1 != std::string::npos && sp2 != std::string::npos)
    resp.status_code = std::stoi(status_line.substr(sp1 + 1, sp2 - sp1 - 1));
  resp.body = raw.substr(hdr_end + 4);
  return resp;
}

static std::string make_auth(const std::vector<uint8_t> &key,
                              const std::string &context) {
  auto now = std::chrono::system_clock::now();
  auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  std::string msg = context + ":" + std::to_string(ts);
  std::vector<uint8_t> data(msg.begin(), msg.end());
  auto h = hmac_sha256(key, data);
  return "Rendezvous " + wininspect::base64::encode(h);
}

// ── Public API ──────────────────────────────────────────────────────────────

bool rendezvous_register(const std::string &url, const std::string &crypto_key,
                          const std::string &uuid, const std::string &name,
                          const std::string &host, int port) {
  auto key = wininspect::base64::decode(crypto_key);
  wininspect::json::Object body;
  body["uuid"] = uuid; body["name"] = name; body["host"] = host; body["port"] = (double)port;
  auto auth = make_auth(key, "register:" + uuid);
  auto resp = http_request("POST", url + "/register", "application/json",
                            wininspect::json::dumps(body), auth);
  bool ok = (resp.status_code == 200 || resp.status_code == 201);
  if (ok) LOG_INFO("Registered with rendezvous: " + url);
  else LOG_WARN("Rendezvous register failed: HTTP " + std::to_string(resp.status_code));
  return ok;
}

bool rendezvous_heartbeat(const std::string &url, const std::string &crypto_key,
                           const std::string &uuid) {
  auto key = wininspect::base64::decode(crypto_key);
  auto auth = make_auth(key, "heartbeat:" + uuid);
  auto resp = http_request("PUT", url + "/heartbeat/" + uuid, "", "", auth);
  return (resp.status_code == 200);
}

bool rendezvous_deregister(const std::string &url, const std::string &crypto_key,
                            const std::string &uuid) {
  auto key = wininspect::base64::decode(crypto_key);
  auto auth = make_auth(key, "deregister:" + uuid);
  auto resp = http_request("DELETE", url + "/instances/" + uuid, "", "", auth);
  return (resp.status_code == 200 || resp.status_code == 204);
}

} // namespace wininspectd
