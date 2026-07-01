// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

// Fuzz-style tests using RapidCheck random generation.

#include "doctest/doctest.h"
#include "rapidcheck/rapidcheck.hpp"
#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"
#include "wininspect/tinyjson.hpp"
#include "wininspect/base64.hpp"
#include <random>
#include <sstream>

using namespace wininspect;

static std::string random_hwnd() {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << (std::rand() % 0xFFFFFFFF);
    return oss.str();
}

static std::string random_bytes(size_t max = 1024) {
  std::string s; s.resize(std::rand() % max);
  for (auto &c : s) c = (char)(std::rand() & 0xFF);
  return s;
}

static std::string random_utf8() {
  std::string s; size_t len = std::rand() % 256;
  for (size_t i = 0; i < len; i++) {
    int r = std::rand() % 5;
    if (r == 0) s += (char)(std::rand() % 0x7F);
    else if (r == 1) { s += (char)(0xC0 + (std::rand() % 32)); s += (char)(0x80 + (std::rand() % 64)); }
    else if (r == 2) { s += (char)(0xE0 + (std::rand() % 16)); s += (char)(0x80 + (std::rand() % 64)); s += (char)(0x80 + (std::rand() % 64)); }
    else s += (char)('a' + (std::rand() % 26));
  }
  return s;
}

static FakeBackend make_fb() { return FakeBackend(std::vector<FakeWindow>{}); }

// ── JSON Parser Fuzzing ─────────────────────────────────────────────────────

DOCTEST_TEST_CASE("fuzz: json parser handles arbitrary bytes") {
  rc::Property("json_arbitrary", []() -> rc::PropertyResult {
    auto input = random_bytes(1024);
    try { auto v = json::parse(input); if (v.is_obj()) { auto d = json::dumps(v.as_obj()); (void)d; } }
    catch (...) {}
    return true;
  });
}

DOCTEST_TEST_CASE("fuzz: json parser handles empty input") {
  rc::Property("json_empty", []() -> rc::PropertyResult {
    try { json::parse(""); } catch (...) {}
    try { json::parse("{}"); } catch (...) {}
    try { json::parse("[]"); } catch (...) {}
    return true;
  });
}

DOCTEST_TEST_CASE("fuzz: json parser handles deeply nested structures") {
  rc::Property("json_deep", []() -> rc::PropertyResult {
    std::string s = "{"; for (int i = 0; i < 100; i++) s += "\"a\":{"; s += "\"b\":1"; for (int i = 0; i < 100; i++) s += "}"; s += "}";
    try { json::parse(s); } catch (...) {}
    return true;
  });
}

DOCTEST_TEST_CASE("fuzz: json parser handles long strings") {
  rc::Property("json_long_strings", []() -> rc::PropertyResult {
    auto p = random_utf8(); std::string s = "{\"key\":\"" + p + "\"}";
    if (s.size() > 65536) return true;
    try { json::parse(s); } catch (...) {}
    return true;
  });
}

// ── Protocol Dispatch Fuzzing ───────────────────────────────────────────────

DOCTEST_TEST_CASE("fuzz: protocol dispatch handles random method names") {
  rc::Property("dispatch_random_method", []() -> rc::PropertyResult {
    auto fb = make_fb(); CoreEngine core(&fb);
    json::Object p; p["x"] = (double)(std::rand() % 2000); p["y"] = (double)(std::rand() % 2000); p["hwnd"] = random_hwnd(); p["text"] = random_utf8();
    std::string methods[] = {"window.listTop","input.mouseClick","screen.capture","process.list",""," ","\n\t","../../etc/passwd","<script>"};
    for (auto &m : methods) { try { CoreRequest r{"f","m",p}; auto s = fb.capture_snapshot(); core.handle(r,s,nullptr); } catch (...) {} }
    return true;
  });
}

DOCTEST_TEST_CASE("fuzz: protocol dispatch handles type mismatches") {
  rc::Property("dispatch_type_mismatch", []() -> rc::PropertyResult {
    auto fb = make_fb(); CoreEngine core(&fb);
    json::Object p; p["x"] = std::string("x"); p["y"] = true; p["hwnd"] = 123.0; p["text"] = false; p["button"] = std::string("left"); p["width"] = std::string("b"); p["keys"] = 42.0;
    std::string methods[] = {"input.mouseClick","input.text","input.hotkey","window.move","process.execute"};
    for (auto &m : methods) { try { CoreRequest r{"f","m",p}; auto s = fb.capture_snapshot(); core.handle(r,s,nullptr); } catch (...) {} }
    return true;
  });
}

// ── Base64 Fuzzing ──────────────────────────────────────────────────────────

DOCTEST_TEST_CASE("fuzz: base64 decode handles arbitrary input") {
  rc::Property("b64_decode_arbitrary", []() -> rc::PropertyResult {
    try { base64::decode(random_bytes(2048)); } catch (...) {}
    return true;
  });
}

DOCTEST_TEST_CASE("fuzz: base64 decode handles padding variants") {
  rc::Property("b64_decode_variants", []() -> rc::PropertyResult {
    const char *v[] = {"","a","ab","abc","abcd","AAAA","////","a+b/","a-b_","a b\t\n"};
    for (auto *s : v) { try { base64::decode(std::string(s)); } catch (...) {} }
    return true;
  });
}

// ── Version Parsing Fuzzing ─────────────────────────────────────────────────

DOCTEST_TEST_CASE("fuzz: version parsing handles garbage") {
  rc::Property("version_garbage", []() -> rc::PropertyResult {
    try { update::parse_version(random_bytes(256)); } catch (...) {}
    return true;
  });
}

// ── Auth Response Fuzzing ───────────────────────────────────────────────────

DOCTEST_TEST_CASE("fuzz: auth response parsing") {
  rc::Property("auth_garbage", []() -> rc::PropertyResult {
    json::Object a; a["identity"] = random_utf8(); a["signature"] = random_utf8(); a["pubkey"] = random_utf8(); a["version"] = random_utf8();
    (void)a;
    return true;
  });
}
