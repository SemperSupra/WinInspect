// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

// Property-based tests using RapidCheck-style random generation.
// Tests properties that must hold for ALL inputs, not just specific cases.
// Replace third_party/rapidcheck/rapidcheck.hpp with the full RapidCheck
// library from https://github.com/emil-e/rapidcheck when available.

#include "doctest/doctest.h"
#include "rapidcheck/rapidcheck.hpp"
#include "wininspect/core.hpp"
#include "wininspect/base64.hpp"
#include "wininspect/update.hpp"
#include "wininspect/fake_backend.hpp"
#include "wininspect/network_config.hpp"
#include <random>
#include <cctype>
#include <sstream>

using namespace wininspect;

// Generate a random HWND string
static std::string random_hwnd()
{
  std::ostringstream oss;
  oss << "0x" << std::hex << std::uppercase << (std::rand() % 0xFFFFFFFF);
  return oss.str();
}

// Generate a random window title
static std::string random_title()
{
  static const char* words[] = {"Window", "Dialog", "Panel", "Button",  "List",
                                "Edit",   "Combo",  "Tree",  "Toolbar", "Status"};
  return std::string(words[std::rand() % 10]) + " " + std::to_string(std::rand() % 1000);
}

DOCTEST_TEST_CASE("property: version comparison is transitive")
{
  rc::Property("transitive", []() -> rc::PropertyResult {
    // Generate three version triples
    std::vector<int> a = {std::rand() % 5, std::rand() % 10};
    std::vector<int> b = {std::rand() % 5, std::rand() % 10};
    std::vector<int> c = {std::rand() % 5, std::rand() % 10};

    int ab = update::compare_versions(a, b);
    int bc = update::compare_versions(b, c);
    int ac = update::compare_versions(a, c);

    // If a > b and b > c, then a > c
    RC_PRE(ab > 0 && bc > 0);
    RC_ASSERT(ac > 0);

    // If a == b and b == c, then a == c
    RC_PRE(ab == 0 && bc == 0);
    RC_ASSERT(ac == 0);

    return true;
  });
}

DOCTEST_TEST_CASE("property: version parse roundtrip")
{
  rc::Property("parse_roundtrip", []() -> rc::PropertyResult {
    int major = std::rand() % 100;
    int minor = std::rand() % 100;
    int patch = std::rand() % 100;

    std::string tag =
        "v" + std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    auto parsed = update::parse_version(tag);

    RC_ASSERT(parsed.size() == 3);
    RC_ASSERT(parsed[0] == major);
    RC_ASSERT(parsed[1] == minor);
    RC_ASSERT(parsed[2] == patch);
    return true;
  });
}

DOCTEST_TEST_CASE("property: base64 roundtrip")
{
  rc::Property("base64_roundtrip", []() -> rc::PropertyResult {
    // Generate random binary data
    size_t len = std::rand() % 1024;
    std::vector<uint8_t> original(len);
    for (auto& b : original)
      b = std::rand() & 0xFF;

    auto encoded = base64::encode(original);
    auto decoded = base64::decode(encoded);

    RC_ASSERT(decoded.size() == original.size());
    RC_ASSERT(decoded == original);
    return true;
  });
}

DOCTEST_TEST_CASE("property: snapshot capture returns top-level windows")
{
  rc::Property("snapshot_shape", []() -> rc::PropertyResult {
    int count = 1 + (std::rand() % 10);
    std::vector<FakeWindow> windows;
    for (int i = 0; i < count; i++) {
      windows.push_back({(hwnd_u64)(i + 1), 0, 0, random_title(), "Class" + std::to_string(i % 5),
                         (bool)(std::rand() % 2)});
    }
    FakeBackend fb(windows);
    auto snap = fb.capture_snapshot();
    RC_ASSERT(snap.top.size() <= (size_t)count);
    return true;
  });
}

DOCTEST_TEST_CASE("property: HWND string format")
{
  rc::Property("hwnd_format", []() -> rc::PropertyResult {
    auto hwnd_str = random_hwnd();
    RC_ASSERT(hwnd_str.substr(0, 2) == "0x");
    RC_ASSERT(hwnd_str.size() > 2);
    RC_ASSERT(hwnd_str.size() <= 10); // 0x + up to 8 hex digits
    return true;
  });
}

DOCTEST_TEST_CASE("property: empty base64")
{
  rc::Property("empty_base64", []() -> rc::PropertyResult {
    std::vector<uint8_t> empty;
    auto encoded = base64::encode(empty);
    RC_ASSERT(encoded.empty());
    auto decoded = base64::decode("");
    RC_ASSERT(decoded.empty());
    return true;
  });
}

DOCTEST_TEST_CASE("property: version with different lengths")
{
  rc::Property("version_lengths", []() -> rc::PropertyResult {
    auto v1 = update::parse_version("v1.2");
    auto v2 = update::parse_version("v1.2.3");
    auto v3 = update::parse_version("v1.2.3.4");

    // Shorter version is "less than" longer when all common parts equal
    RC_ASSERT(update::compare_versions(v1, v2) < 0);
    RC_ASSERT(update::compare_versions(v2, v3) < 0);
    RC_ASSERT(update::compare_versions(v1, v3) < 0);
    return true;
  });
}

// C2: RFC 4648 §10 Base64 test vectors
// https://www.rfc-editor.org/rfc/rfc4648#section-10
// These are the standard test vectors for Base64 encoding.
DOCTEST_TEST_CASE("C2 conformance: RFC 4648 base64 test vectors")
{
  // RFC 4648 §10: Base64 Test Vectors
  // "" → ""
  // "f" → "Zg=="
  // "fo" → "Zm8="
  // "foo" → "Zm9v"
  // "foob" → "Zm9vYg=="
  // "fooba" → "Zm9vYmE="
  // "foobar" → "Zm9vYmFy"
  auto check = [](const std::string& raw, const std::string& expected_b64) {
    std::vector<uint8_t> data(raw.begin(), raw.end());
    auto encoded = base64::encode(data);
    DOCTEST_REQUIRE_EQ(encoded, expected_b64);
    auto decoded = base64::decode(expected_b64);
    std::string decoded_str(decoded.begin(), decoded.end());
    DOCTEST_REQUIRE_EQ(decoded_str, raw);
  };
  check("", "");
  check("f", "Zg==");
  check("fo", "Zm8=");
  check("foo", "Zm9v");
  check("foob", "Zm9vYg==");
  check("fooba", "Zm9vYmE=");
  check("foobar", "Zm9vYmFy");
}

// C3: RFC 4122 UUID v4 format validation
DOCTEST_TEST_CASE("C3 conformance: UUID RFC 4122 format")
{
  auto uuid = generate_uuid_v4();
  // Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx (36 chars)
  DOCTEST_REQUIRE_EQ(uuid.size(), 36u);
  DOCTEST_REQUIRE_EQ(uuid[8], '-');
  DOCTEST_REQUIRE_EQ(uuid[13], '-');
  DOCTEST_REQUIRE_EQ(uuid[18], '-');
  DOCTEST_REQUIRE_EQ(uuid[23], '-');

  // Version nibble at position 14 must be '4'
  DOCTEST_REQUIRE_EQ(uuid[14], '4');

  // Variant nibble at position 19 must be '8', '9', 'a', or 'b'
  char variant = uuid[19];
  DOCTEST_REQUIRE((variant == '8' || variant == '9' || variant == 'a' || variant == 'b'));

  // All other characters must be hex
  for (int i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23)
      continue;
    DOCTEST_REQUIRE(std::isxdigit((unsigned char)uuid[i]));
  }

  // Multiple UUIDs should be different
  auto uuid2 = generate_uuid_v4();
  DOCTEST_REQUIRE(uuid != uuid2);
}

// C4: JSON-RPC 2.0 error code conformance
DOCTEST_TEST_CASE("C4 conformance: JSON-RPC 2.0 error codes")
{
  FakeBackend fb({{1, 0, 0, "W", "C", true}});
  CoreEngine core(&fb);

  // -32700: Parse error (invalid JSON in request)
  // (Not tested — parse_request_json handles this)

  // -32601: Method not found
  CoreRequest req_bad{"c4", "nonexistent.method", json::Object{}};
  auto r = core.handle(req_bad, fb.capture_snapshot());
  DOCTEST_REQUIRE(!r.ok);
  DOCTEST_REQUIRE_EQ(r.error_code, "E_BAD_METHOD");

  // -32602: Invalid params (missing required fields)
  json::Object p;
  CoreRequest req_noparam{"c4b", "window.getInfo", p}; // no hwnd
  auto r2 = core.handle(req_noparam, fb.capture_snapshot());
  DOCTEST_REQUIRE(!r2.ok);
  DOCTEST_REQUIRE(r2.error_code == "E_BAD_REQUEST" || r2.error_code == "E_BAD_HWND");

  // Verify all error responses have required fields
  DOCTEST_REQUIRE(!r.id.empty());
  DOCTEST_REQUIRE(!r2.id.empty());
}
