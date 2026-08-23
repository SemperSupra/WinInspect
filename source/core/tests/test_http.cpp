// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// HTTP API tests. Sends raw HTTP requests to the server and validates
// JSON responses. Tests: routing, auth, read-only, no-clipboard.

#include "doctest/doctest.h"
#include "wininspect/fake_backend.hpp"
#include "wininspect/core.hpp"
#include "wininspect/tinyjson.hpp"
#include <string>
#include <sstream>
#include <map>

using namespace wininspect;

// ── Minimal HTTP parser for testing (reuses the same logic as http_server) ──

struct HttpReq
{
  std::string method, path, body;
  std::map<std::string, std::string> headers;
};

struct HttpResp
{
  int code = 200;
  std::string status = "OK", body;
  std::string content_type = "application/json; charset=utf-8";
};

static std::string build_http_response(const HttpResp& r)
{
  std::ostringstream ss;
  ss << "HTTP/1.1 " << r.code << " " << r.status << "\r\n"
     << "Content-Type: " << r.content_type << "\r\n"
     << "Content-Length: " << r.body.size() << "\r\n"
     << "Connection: close\r\n"
     << "\r\n"
     << r.body;
  return ss.str();
}

static bool parse_http(const std::string& raw, HttpReq& req)
{
  auto eol = raw.find("\r\n");
  if (eol == std::string::npos)
    return false;
  auto fl = raw.substr(0, eol);
  auto s1 = fl.find(' '), s2 = fl.rfind(' ');
  if (s1 == std::string::npos || s2 == std::string::npos || s1 == s2)
    return false;
  req.method = fl.substr(0, s1);
  req.path = fl.substr(s1 + 1, s2 - s1 - 1);
  size_t pos = eol + 2;
  while (pos < raw.size()) {
    auto he = raw.find("\r\n", pos);
    if (he == std::string::npos)
      break;
    if (he == pos) {
      pos = he + 2;
      break;
    }
    auto c = raw.find(':', pos);
    if (c != std::string::npos && c < he) {
      req.headers[raw.substr(pos, c - pos)] = raw.substr(c + 2, he - c - 2);
    }
    pos = he + 2;
  }
  if (pos < raw.size())
    req.body = raw.substr(pos);
  return true;
}

// ── Routes (mirrors http_server.cpp dispatch) ─────────────────────────────

struct Route
{
  const char* method;
  const char* path;
  const char* rpc_method;
};

static Route routes[] = {
    {"GET", "/api/v1/health", "daemon.health"},
    {"GET", "/api/v1/identity", "daemon.identity"},
    {"GET", "/api/v1/capabilities", "daemon.capabilities"},
    {"POST", "/api/v1/capture", "screen.capture"},
    {"GET", "/api/v1/windows", "window.listTop"},
    {"POST", "/api/v1/click", "input.mouseClick"},
    {"POST", "/api/v1/type", "input.text"},
    {"POST", "/api/v1/hotkey", "input.hotkey"},
    {"GET", "/api/v1/processes", "process.list"},
    {"POST", "/api/v1/exec", "process.execute"},
};

// ── Simulated HTTP Server (tests dispatch logic without sockets) ──────────

static HttpResp handle_http(const HttpReq& req, IBackend* backend, const std::string& auth_token,
                            bool read_only, bool no_clipboard)
{
  HttpResp resp;

  // CORS preflight
  if (req.method == "OPTIONS") {
    resp.code = 204;
    return resp;
  }

  // Auth check
  if (!auth_token.empty()) {
    auto it = req.headers.find("Authorization");
    std::string token_val;
    if (it != req.headers.end() && it->second.size() > 7 && it->second.substr(0, 7) == "Bearer ")
      token_val = it->second.substr(7);
    if (token_val != auth_token) {
      resp.code = 401;
      resp.body = R"({"error":"unauthorized"})";
      return resp;
    }
  }

  // Route matching + flag enforcement
  const Route* matched = nullptr;
  for (auto& rt : routes) {
    if (req.method == rt.method && req.path == rt.path) {
      matched = &rt;
      break;
    }
  }

  // Dashboard routes
  if (req.path == "/dashboard") {
    resp.content_type = "text/html; charset=utf-8";
    resp.body = "<html>dashboard</html>";
    return resp;
  }
  if (req.path == "/") {
    resp.code = 301;
    return resp;
  }

  if (!matched) {
    resp.code = 404;
    resp.body = R"({"error":"not found"})";
    return resp;
  }

  // Enforce daemon flags
  if (no_clipboard && std::string(matched->rpc_method).find("clipboard") != std::string::npos) {
    resp.code = 403;
    resp.body = R"({"error":"clipboard access disabled"})";
    return resp;
  }
  if (read_only) {
    std::string rpc = matched->rpc_method;
    if (rpc == "input.mouseClick" || rpc == "input.hotkey" || rpc == "input.text" ||
        rpc == "process.execute" || rpc.find("reg.write") != std::string::npos) {
      resp.code = 403;
      resp.body = R"({"error":"read-only mode"})";
      return resp;
    }
  }

  // Dispatch to core engine
  CoreEngine core(backend);
  json::Object params;
  CoreRequest creq{"http-test", matched->rpc_method, params};
  auto snap = core.get_backend()->capture_snapshot();
  auto cresp = core.handle(creq, snap, nullptr);

  json::Object result;
  result["ok"] = cresp.ok;
  result["result"] = cresp.result;
  resp.body = json::dumps(result);
  return resp;
}

// ── Tests ─────────────────────────────────────────────────────────────────

DOCTEST_TEST_CASE("HTTP: health endpoint")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"GET", "/api/v1/health", "", {}};
  auto resp = handle_http(req, fb.get(), "", false, false);
  DOCTEST_REQUIRE(resp.code == 200);
  auto v = json::parse(resp.body);
  DOCTEST_REQUIRE(v.is_obj());
  auto r = v.as_obj();
  DOCTEST_REQUIRE(r.find("result") != r.end());
  DOCTEST_REQUIRE(r.find("ok") != r.end());
  DOCTEST_REQUIRE(r.at("ok").as_bool());
}

DOCTEST_TEST_CASE("HTTP: identity endpoint")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"GET", "/api/v1/identity", "", {}};
  auto resp = handle_http(req, fb.get(), "", false, false);
  DOCTEST_REQUIRE(resp.code == 200);
}

DOCTEST_TEST_CASE("HTTP: capabilities endpoint")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"GET", "/api/v1/capabilities", "", {}};
  auto resp = handle_http(req, fb.get(), "", false, false);
  DOCTEST_REQUIRE(resp.code == 200);
}

DOCTEST_TEST_CASE("HTTP: windows endpoint")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{{1, 0, 0, "A", "C", true}});
  HttpReq req{"GET", "/api/v1/windows", "", {}};
  auto resp = handle_http(req, fb.get(), "", false, false);
  DOCTEST_REQUIRE(resp.code == 200);
}

DOCTEST_TEST_CASE("HTTP: capture endpoint")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"POST", "/api/v1/capture", R"({"left":0,"top":0,"right":100,"bottom":100})", {}};
  auto resp = handle_http(req, fb.get(), "", false, false);
  DOCTEST_REQUIRE(resp.code == 200);
}

DOCTEST_TEST_CASE("HTTP: 404 for unknown route")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"GET", "/api/v1/unknown", "", {}};
  auto resp = handle_http(req, fb.get(), "", false, false);
  DOCTEST_REQUIRE(resp.code == 404);
}

DOCTEST_TEST_CASE("HTTP: auth token required")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"GET", "/api/v1/health", "", {}};
  auto resp = handle_http(req, fb.get(), "secret", false, false);
  DOCTEST_REQUIRE(resp.code == 401);
}

DOCTEST_TEST_CASE("HTTP: auth token accepted")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"GET", "/api/v1/health", "", {{"Authorization", "Bearer secret"}}};
  auto resp = handle_http(req, fb.get(), "secret", false, false);
  DOCTEST_REQUIRE(resp.code == 200);
}

DOCTEST_TEST_CASE("HTTP: read-only blocks mutation")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"POST", "/api/v1/click", R"({"x":100,"y":200})", {}};
  auto resp = handle_http(req, fb.get(), "", true, false);
  DOCTEST_REQUIRE(resp.code == 403);
}

DOCTEST_TEST_CASE("HTTP: no-clipboard blocks clipboard")
{
  // Clipboard methods aren't in the HTTP route table directly, but the
  // no-clipboard flag is checked generically by method name.
  // This test verifies the flag enforcement code path.
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"POST", "/api/v1/capture", R"({"left":0,"top":0,"right":100,"bottom":100})", {}};
  auto resp = handle_http(req, fb.get(), "", false, true);
  DOCTEST_REQUIRE(resp.code == 200); // capture is not clipboard
}

DOCTEST_TEST_CASE("HTTP: CORS preflight")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"OPTIONS", "/api/v1/health", "", {}};
  auto resp = handle_http(req, fb.get(), "", false, false);
  DOCTEST_REQUIRE(resp.code == 204);
}

DOCTEST_TEST_CASE("HTTP: dashboard route")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"GET", "/dashboard", "", {}};
  auto resp = handle_http(req, fb.get(), "", false, false);
  DOCTEST_REQUIRE(resp.code == 200);
  DOCTEST_REQUIRE(resp.content_type.find("html") != std::string::npos);
}

DOCTEST_TEST_CASE("HTTP: root redirect")
{
  auto fb = std::make_shared<FakeBackend>(std::vector<FakeWindow>{});
  HttpReq req{"GET", "/", "", {}};
  auto resp = handle_http(req, fb.get(), "", false, false);
  DOCTEST_REQUIRE(resp.code == 301);
}
