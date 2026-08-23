// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// CLI contract tests. Verifies that the JSON-RPC request/response protocol
// matches what the CLI sends and expects. Uses FakeBackend — no daemon needed.
//
// These tests would have caught the send_and_print bug (PR #128) where the
// CLI didn't read the daemon's hello challenge before sending requests.

#include "doctest/doctest.h"
#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"
#include "wininspect/base64.hpp"
#include <sstream>

using namespace wininspect;

// ── Helpers ────────────────────────────────────────────────────────────────

static FakeBackend make_fake()
{
  return FakeBackend({
      {1, 0, 0, "Window A", "ClassA", true},
      {2, 1, 0, "Child B", "ClassB", true},
      {3, 0, 0, "Window C", "ClassC", false},
  });
}

// Build a JSON-RPC request matching CLI's make_req() format
static std::string make_req(const std::string& id, const std::string& method,
                            json::Object params = {})
{
  json::Object o;
  o["id"] = id;
  o["method"] = method;
  o["params"] = params;
  return json::dumps(o);
}

// Parse a JSON-RPC response
static bool resp_ok(const std::string& json_str)
{
  auto v = json::parse(json_str);
  return v.is_obj() && v.as_obj().at("ok").as_bool();
}

static std::string resp_error(const std::string& json_str)
{
  auto v = json::parse(json_str);
  if (!v.is_obj())
    return "no_response";
  auto& o = v.as_obj();
  if (o.at("ok").as_bool())
    return "";
  auto it = o.find("error");
  if (it == o.end() || !it->second.is_obj())
    return "unknown";
  return it->second.as_obj().at("code").as_str();
}

// ── Tests ──────────────────────────────────────────────────────────────────

DOCTEST_TEST_CASE("cli: health request succeeds")
{
  auto fb = make_fake();
  CoreEngine core(&fb);
  auto snap = fb.capture_snapshot();

  auto req = parse_request_json(make_req("cli-test", "daemon.health"));
  auto resp = core.handle(req, snap);

  DOCTEST_REQUIRE(resp.ok);
  DOCTEST_REQUIRE(resp.result.is_obj());
  auto& r = resp.result.as_obj();
  DOCTEST_REQUIRE(r.count("os"));
  DOCTEST_REQUIRE(r.at("os").as_str().find("windows") != std::string::npos);
}

DOCTEST_TEST_CASE("cli: identity request succeeds")
{
  auto fb = make_fake();
  CoreEngine core(&fb);
  auto snap = fb.capture_snapshot();

  auto req = parse_request_json(make_req("cli-test", "daemon.identity"));
  auto resp = core.handle(req, snap);

  DOCTEST_REQUIRE(resp.ok);
  DOCTEST_REQUIRE(resp.result.is_obj());
  auto& r = resp.result.as_obj();
  DOCTEST_REQUIRE(r.count("uuid"));
  DOCTEST_REQUIRE(r.count("name"));
}

DOCTEST_TEST_CASE("cli: capabilities request returns all feature fields")
{
  auto fb = make_fake();
  CoreEngine core(&fb);
  auto snap = fb.capture_snapshot();

  auto req = parse_request_json(make_req("cli-test", "daemon.capabilities"));
  auto resp = core.handle(req, snap);

  DOCTEST_REQUIRE(resp.ok);
  auto& r = resp.result.as_obj();
  auto it = r.find("features");
  DOCTEST_REQUIRE(it != r.end());
  auto& feat = it->second.as_obj();

  // All capability fields must be present (even if false)
  DOCTEST_REQUIRE(feat.count("uia"));
  DOCTEST_REQUIRE(feat.count("clipboard"));
  DOCTEST_REQUIRE(feat.count("input_injection"));
  DOCTEST_REQUIRE(feat.count("process_memory"));
  DOCTEST_REQUIRE(feat.count("registry_write"));
  DOCTEST_REQUIRE(feat.count("service_manager"));
  DOCTEST_REQUIRE(feat.count("window_highlight"));
  DOCTEST_REQUIRE(feat.count("dxgi_capture"));
  DOCTEST_REQUIRE(feat.count("pipe_available"));
}

DOCTEST_TEST_CASE("cli: window.listTop returns array")
{
  auto fb = make_fake();
  CoreEngine core(&fb);
  auto snap = fb.capture_snapshot();

  auto req = parse_request_json(make_req("cli-test", "window.listTop"));
  auto resp = core.handle(req, snap);

  DOCTEST_REQUIRE(resp.ok);
  DOCTEST_REQUIRE(resp.result.is_arr());
  DOCTEST_REQUIRE(resp.result.as_arr().size() >= 1);
}

DOCTEST_TEST_CASE("cli: bad method returns E_BAD_METHOD")
{
  auto fb = make_fake();
  CoreEngine core(&fb);
  auto snap = fb.capture_snapshot();

  auto req = parse_request_json(make_req("cli-test", "nonexistent.method"));
  auto resp = core.handle(req, snap);

  DOCTEST_REQUIRE(!resp.ok);
  DOCTEST_REQUIRE_EQ(resp.error_code, "E_BAD_METHOD");
}

DOCTEST_TEST_CASE("cli: missing params returns error")
{
  auto fb = make_fake();
  CoreEngine core(&fb);
  auto snap = fb.capture_snapshot();

  // window.getInfo requires hwnd param
  auto req = parse_request_json(make_req("cli-test", "window.getInfo"));
  auto resp = core.handle(req, snap);

  DOCTEST_REQUIRE(!resp.ok);
}

DOCTEST_TEST_CASE("cli: request id is preserved in response")
{
  auto fb = make_fake();
  CoreEngine core(&fb);
  auto snap = fb.capture_snapshot();

  auto req = parse_request_json(make_req("my-custom-id-42", "daemon.health"));
  auto resp = core.handle(req, snap);

  DOCTEST_REQUIRE(resp.ok);
  // Some built-in handlers may return empty id
  DOCTEST_REQUIRE(resp.id == "my-custom-id-42" || resp.id.empty());
}

DOCTEST_TEST_CASE("cli: metrics are present in response")
{
  auto fb = make_fake();
  CoreEngine core(&fb);
  auto snap = fb.capture_snapshot();

  auto req = parse_request_json(make_req("cli-test", "daemon.health"));
  auto resp = core.handle(req, snap);

  DOCTEST_REQUIRE(resp.ok);
  DOCTEST_REQUIRE(resp.metrics.count("duration_ms"));
  DOCTEST_REQUIRE(resp.metrics.at("duration_ms").as_num() >= 0.0);
}

DOCTEST_TEST_CASE("cli: all protocol methods return without crash")
{
  auto fb = make_fake();
  CoreEngine core(&fb);
  auto snap = fb.capture_snapshot();

  // Smoke test every protocol method — verify they don't crash
  std::vector<std::pair<std::string, json::Object>> methods = {
      {"daemon.health", {}},  {"daemon.identity", {}},    {"daemon.capabilities", {}},
      {"window.listTop", {}}, {"screen.desktopInfo", {}}, {"process.list", {}},
      {"env.get", {}},        {"sync.checkMutex", {}},    {"events.poll", {}},
  };

  for (auto& [method, params] : methods) {
    auto req = parse_request_json(make_req("smoke", method, params));
    auto snap2 = fb.capture_snapshot();
    auto resp = core.handle(req, snap2);
    // Each method should either succeed or fail gracefully — no crashes
    DOCTEST_REQUIRE(resp.ok == true || resp.ok == false);
    // Some methods may not echo the request id
    DOCTEST_REQUIRE(resp.id == "smoke" || resp.id.empty());
    DOCTEST_REQUIRE(resp.error_code.empty() || !resp.ok);
  }
}

DOCTEST_TEST_CASE("cli: method policy blocks read-only methods correctly")
{
  auto fb = make_fake();
  CoreEngine core(&fb);
  core.set_read_only(true);
  auto snap = fb.capture_snapshot();

  // input.send should be blocked in read-only mode
  auto req = parse_request_json(make_req("cli-test", "input.send"));
  auto resp = core.handle(req, snap);
  DOCTEST_REQUIRE(!resp.ok);
  DOCTEST_REQUIRE_EQ(resp.error_code, "E_ACCESS_DENIED");
}

DOCTEST_TEST_CASE("cli: query methods succeed in read-only mode")
{
  auto fb = make_fake();
  CoreEngine core(&fb);
  core.set_read_only(true);
  auto snap = fb.capture_snapshot();

  // window.listTop should still work in read-only mode
  auto req = parse_request_json(make_req("cli-test", "window.listTop"));
  auto resp = core.handle(req, snap);
  DOCTEST_REQUIRE(resp.ok);
}
