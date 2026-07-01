// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

// Lightweight HTTP server for REST API access to the daemon.
//
// Usage: wininspectd --http-port 8080 [--http-token secret]
//
// Endpoints:
//   GET    /api/v1/health       → daemon.health
//   GET    /api/v1/identity     → daemon.identity
//   GET    /api/v1/capabilities → daemon.capabilities
//   POST   /api/v1/capture      → screen.capture
//   GET    /api/v1/windows      → window.listTop
//   POST   /api/v1/click        → input.mouseClick
//   POST   /api/v1/type         → input.text
//   POST   /api/v1/hotkey       → input.hotkey
//   GET    /api/v1/processes    → process.list
//   POST   /api/v1/exec         → process.execute

#include "wininspect/core.hpp"
#include "wininspect/logger.hpp"
#include "wininspect/tinyjson.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <string>
#include <map>
#include <functional>
#include <sstream>
#include <atomic>
#include <thread>
#include <cstring>

using namespace wininspect;

namespace wininspectd {

// ── HTTP Types ──────────────────────────────────────────────────────────────

struct HttpReq {
  std::string method, path, body;
  std::map<std::string, std::string> headers;
};

struct HttpResp {
  int code = 200;
  std::string status = "OK", body;
  std::string content_type = "application/json; charset=utf-8";
};

static std::string build_response(const HttpResp &r) {
  std::ostringstream ss;
  ss << "HTTP/1.1 " << r.code << " " << r.status << "\r\n"
     << "Content-Type: " << r.content_type << "\r\n"
     << "Content-Length: " << r.body.size() << "\r\n"
     << "Connection: close\r\n"
     << "Access-Control-Allow-Origin: *\r\n"
     << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
     << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
     << "\r\n" << r.body;
  return ss.str();
}

static bool parse_http(const std::string &raw, HttpReq &req) {
  auto eol = raw.find("\r\n");
  if (eol == std::string::npos) return false;
  auto fl = raw.substr(0, eol);
  auto s1 = fl.find(' '), s2 = fl.rfind(' ');
  if (s1 == std::string::npos || s2 == std::string::npos || s1 == s2) return false;
  req.method = fl.substr(0, s1);
  req.path = fl.substr(s1 + 1, s2 - s1 - 1);
  size_t pos = eol + 2;
  while (pos < raw.size()) {
    auto he = raw.find("\r\n", pos);
    if (he == std::string::npos) break;
    if (he == pos) { pos = he + 2; break; }
    auto c = raw.find(':', pos);
    if (c != std::string::npos && c < he) {
      req.headers[raw.substr(pos, c - pos)] = raw.substr(c + 2, he - c - 2);
    }
    pos = he + 2;
  }
  if (pos < raw.size()) req.body = raw.substr(pos);
  return true;
}

// ── Route Table ─────────────────────────────────────────────────────────────

struct Route {
  const char *method;
  const char *path;
  const char *rpc_method;  // core dispatch method
  std::function<void(const HttpReq&, json::Object&)> param_fill;
};

static json::Object json_from(const HttpReq &req) {
  if (req.body.empty()) return json::Object{};
  try {
    auto v = json::parse(req.body);
    if (v.is_obj()) return v.as_obj();
  } catch (...) {}
  return json::Object{};
}

// ── HTTP Server ─────────────────────────────────────────────────────────────

void run_http_server(std::atomic<bool> *running, int port,
                      CoreEngine &core, const std::string &auth_token) {
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) { LOG_ERROR("HTTP: Winsock init failed"); return; }

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) { WSACleanup(); return; }

  int on = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((u_short)port);

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    LOG_ERROR("HTTP: bind failed on port " + std::to_string(port));
    closesocket(s); WSACleanup(); return;
  }
  listen(s, SOMAXCONN);

  u_long mode = 1;
  ioctlsocket(s, FIONBIO, &mode);

  auto snapshot = core.get_backend()->capture_snapshot();

  // Route table: method, path, rpc_method, param_filler
  Route routes[] = {
    {"GET", "/api/v1/health", "daemon.health", nullptr},
    {"GET", "/api/v1/identity", "daemon.identity", nullptr},
    {"GET", "/api/v1/capabilities", "daemon.capabilities", nullptr},
    {"POST", "/api/v1/capture", "screen.capture", nullptr},
    {"GET", "/api/v1/windows", "window.listTop", nullptr},
    {"POST", "/api/v1/click", "input.mouseClick", [](const HttpReq &r, json::Object &p) {
      auto j = json_from(r);
      auto it = j.find("x"); if (it != j.end()) p["x"] = it->second;
      it = j.find("y"); if (it != j.end()) p["y"] = it->second;
      it = j.find("button"); if (it != j.end()) p["button"] = it->second;
    }},
    {"POST", "/api/v1/type", "input.text", [](const HttpReq &r, json::Object &p) {
      auto j = json_from(r);
      auto it = j.find("text"); if (it != j.end()) p["text"] = it->second;
    }},
    {"POST", "/api/v1/hotkey", "input.hotkey", [](const HttpReq &r, json::Object &p) {
      auto j = json_from(r);
      auto it = j.find("keys"); if (it != j.end()) p["keys"] = it->second;
    }},
    {"GET", "/api/v1/processes", "process.list", nullptr},
    {"POST", "/api/v1/exec", "process.execute", [](const HttpReq &r, json::Object &p) {
      auto j = json_from(r);
      auto it = j.find("command"); if (it != j.end()) p["command"] = it->second;
      it = j.find("args"); if (it != j.end()) p["args"] = it->second;
    }},
  };

  LOG_INFO("HTTP server listening on port " + std::to_string(port));

  while (running->load()) {
    SOCKET c = accept(s, nullptr, nullptr);
    if (c == INVALID_SOCKET) { Sleep(100); continue; }

    char buf[8192];
    int r = recv(c, buf, sizeof(buf) - 1, 0);
    if (r <= 0) { closesocket(c); continue; }
    buf[r] = '\0';

    HttpReq req;
    HttpResp resp;

    if (!parse_http(std::string(buf), req)) {
      resp.code = 400; resp.status = "Bad Request"; resp.body = R"({"error":"bad request"})";
      std::string out = build_response(resp);
      send(c, out.data(), (int)out.size(), 0);
      closesocket(c); continue;
    }

    // CORS preflight
    if (req.method == "OPTIONS") {
      resp.code = 204;
      std::string out = build_response(resp);
      send(c, out.data(), (int)out.size(), 0);
      closesocket(c); continue;
    }

    // Auth check
    if (!auth_token.empty()) {
      auto it = req.headers.find("Authorization");
      std::string token_val;
      if (it != req.headers.end() && it->second.size() > 7 && it->second.substr(0,7) == "Bearer ")
        token_val = it->second.substr(7);
      if (token_val != auth_token) {
        resp.code = 401; resp.status = "Unauthorized"; resp.body = R"({"error":"unauthorized"})";
        std::string out = build_response(resp);
        send(c, out.data(), (int)out.size(), 0);
        closesocket(c); continue;
      }
    }

    // Find matching route
    const Route *matched = nullptr;
    for (auto &rt : routes) {
      if (req.method == rt.method && req.path == rt.path) { matched = &rt; break; }
    }

    if (!matched) {
      resp.code = 404; resp.status = "Not Found"; resp.body = R"({"error":"not found"})";
      std::string out = build_response(resp);
      send(c, out.data(), (int)out.size(), 0);
      closesocket(c); continue;
    }

    // Dispatch to core engine
    try {
      json::Object params;
      if (matched->param_fill) matched->param_fill(req, params);
      CoreRequest creq{"http-1", matched->rpc_method, params};
      auto snap = core.get_backend()->capture_snapshot();
      auto cresp = core.handle(creq, snap, nullptr);

      json::Object result;
      result["ok"] = cresp.ok;
      result["id"] = cresp.id;
      result["result"] = cresp.result;
      if (!cresp.error_code.empty()) result["error_code"] = cresp.error_code;
      if (!cresp.error_message.empty()) result["error_message"] = cresp.error_message;
      resp.body = json::dumps(result);
    } catch (const std::exception &e) {
      json::Object err;
      err["ok"] = false; err["error"] = std::string(e.what());
      resp.body = json::dumps(err);
    }

    std::string out = build_response(resp);
    send(c, out.data(), (int)out.size(), 0);
    closesocket(c);
  }

  closesocket(s); WSACleanup();
}

} // namespace wininspectd
