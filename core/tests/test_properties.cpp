// SPDX-License-Identifier: Apache-2.0
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
#include <random>
#include <sstream>

using namespace wininspect;

// Generate a random HWND string
static std::string random_hwnd() {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase
        << (std::rand() % 0xFFFFFFFF);
    return oss.str();
}

// Generate a random window title
static std::string random_title() {
    static const char* words[] = {"Window", "Dialog", "Panel", "Button",
        "List", "Edit", "Combo", "Tree", "Toolbar", "Status"};
    return std::string(words[std::rand() % 10]) +
           " " + std::to_string(std::rand() % 1000);
}

DOCTEST_TEST_CASE("property: version comparison is transitive") {
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

DOCTEST_TEST_CASE("property: version parse roundtrip") {
    rc::Property("parse_roundtrip", []() -> rc::PropertyResult {
        int major = std::rand() % 100;
        int minor = std::rand() % 100;
        int patch = std::rand() % 100;

        std::string tag = "v" + std::to_string(major) + "." +
                          std::to_string(minor) + "." +
                          std::to_string(patch);
        auto parsed = update::parse_version(tag);

        RC_ASSERT(parsed.size() == 3);
        RC_ASSERT(parsed[0] == major);
        RC_ASSERT(parsed[1] == minor);
        RC_ASSERT(parsed[2] == patch);
        return true;
    });
}

DOCTEST_TEST_CASE("property: base64 roundtrip") {
    rc::Property("base64_roundtrip", []() -> rc::PropertyResult {
        // Generate random binary data
        size_t len = std::rand() % 1024;
        std::vector<uint8_t> original(len);
        for (auto& b : original) b = std::rand() & 0xFF;

        auto encoded = base64::encode(original);
        auto decoded = base64::decode(encoded);

        RC_ASSERT(decoded.size() == original.size());
        RC_ASSERT(decoded == original);
        return true;
    });
}

DOCTEST_TEST_CASE("property: snapshot capture returns top-level windows") {
    rc::Property("snapshot_shape", []() -> rc::PropertyResult {
        int count = 1 + (std::rand() % 10);
        std::vector<FakeWindow> windows;
        for (int i = 0; i < count; i++) {
            windows.push_back({(hwnd_u64)(i + 1), 0, 0,
                               random_title(), "Class" + std::to_string(i % 5),
                               (bool)(std::rand() % 2)});
        }
        FakeBackend fb(windows);
        auto snap = fb.capture_snapshot();
        RC_ASSERT(snap.top.size() <= (size_t)count);
        return true;
    });
}

DOCTEST_TEST_CASE("property: HWND string format") {
    rc::Property("hwnd_format", []() -> rc::PropertyResult {
        auto hwnd_str = random_hwnd();
        RC_ASSERT(hwnd_str.substr(0, 2) == "0x");
        RC_ASSERT(hwnd_str.size() > 2);
        RC_ASSERT(hwnd_str.size() <= 10); // 0x + up to 8 hex digits
        return true;
    });
}

DOCTEST_TEST_CASE("property: empty base64") {
    rc::Property("empty_base64", []() -> rc::PropertyResult {
        std::vector<uint8_t> empty;
        auto encoded = base64::encode(empty);
        RC_ASSERT(encoded.empty());
        auto decoded = base64::decode("");
        RC_ASSERT(decoded.empty());
        return true;
    });
}

DOCTEST_TEST_CASE("property: version with different lengths") {
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
