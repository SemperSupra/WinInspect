// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// Configuration and CLI flag tests. Tests apply_cli_overrides for all
// major flags, config file load/save round-trip, and the resolution helpers.

#include "doctest/doctest.h"
#include "wininspect/network_config.hpp"
#include "network_config.hpp" // daemon's apply_cli_overrides
#include "wininspect/core.hpp"

#ifdef _WIN32
// Helper: simulate argc/argv for testing
#include <vector>
#include <cstring>

using namespace wininspect;

// ── Config Resolution Helpers ─────────────────────────────────────────────
// These are tested independently of the daemon startup path.

DOCTEST_TEST_CASE("config: resolve_int uses cli first")
{
  int result = resolve_int("1985", "WININSPECT_PORT", 9090, 1234);
  DOCTEST_REQUIRE_EQ(result, 1985);
}

DOCTEST_TEST_CASE("config: resolve_int falls through to cfg")
{
  int result = resolve_int("", "WININSPECT_NONEXIST", 9090, 1234);
  DOCTEST_REQUIRE_EQ(result, 9090);
}

DOCTEST_TEST_CASE("config: resolve_int falls through to default")
{
  int result = resolve_int("", "WININSPECT_NONEXIST", 0, 1234);
  DOCTEST_REQUIRE_EQ(result, 1234);
}

DOCTEST_TEST_CASE("config: resolve_config uses cli first")
{
  auto result = resolve_config("cli-val", "WININSPECT_TEST", "cfg-val", "default");
  DOCTEST_REQUIRE_EQ(result, "cli-val");
}

DOCTEST_TEST_CASE("config: resolve_config falls through to cfg")
{
  auto result = resolve_config("", "WININSPECT_NONEXIST", "cfg-val", "default");
  DOCTEST_REQUIRE_EQ(result, "cfg-val");
}

DOCTEST_TEST_CASE("config: resolve_config falls through to default")
{
  auto result = resolve_config("", "WININSPECT_NONEXIST", "", "default");
  DOCTEST_REQUIRE_EQ(result, "default");
}

// ── Config File Round-Trip ───────────────────────────────────────────────

DOCTEST_TEST_CASE("config: save and load round-trip")
{
  NetworkConfig original;
  original.port = 19999;
  original.discovery_port = 20000;
  original.http_port = 8080;
  original.include_hostname = true;
  original.rate_limit_ms = 500;

  std::string test_path = std::string(std::getenv("TEMP")) + "\\test_config_roundtrip.json";
  save_config(test_path, original);
  auto loaded = load_config(test_path);
  DOCTEST_REQUIRE_EQ(loaded.port, original.port);
  DOCTEST_REQUIRE_EQ(loaded.discovery_port, original.discovery_port);
  DOCTEST_REQUIRE_EQ(loaded.http_port, original.http_port);
  DOCTEST_REQUIRE_EQ(loaded.include_hostname, original.include_hostname);
  DOCTEST_REQUIRE_EQ(loaded.rate_limit_ms, original.rate_limit_ms);
  std::remove(test_path.c_str());
}

DOCTEST_TEST_CASE("config: save and load with identity")
{
  NetworkConfig cfg;
  cfg.identity.name = "test-instance";
  cfg.identity.hostname = "testhost";
  cfg.identity.uuid = "00000000-0000-4000-8000-000000000000";

  std::string test_path = std::string(std::getenv("TEMP")) + "\\test_config_identity.json";
  save_config(test_path, cfg);
  auto loaded = load_config(test_path);
  DOCTEST_REQUIRE_EQ(loaded.identity.name, "test-instance");
  DOCTEST_REQUIRE_EQ(loaded.identity.hostname, "testhost");
  DOCTEST_REQUIRE_EQ(loaded.identity.uuid, "00000000-0000-4000-8000-000000000000");
  std::remove(test_path.c_str());
}

DOCTEST_TEST_CASE("config: load nonexistent returns defaults")
{
  auto cfg = load_config("nonexistent_config_file.json");
  DOCTEST_REQUIRE_EQ(cfg.port, 1985);
  DOCTEST_REQUIRE_EQ(cfg.discovery_port, 1986);
}

// ── CLI Override Tests ───────────────────────────────────────────────────

DOCTEST_TEST_CASE("config: apply_cli_overrides port")
{
  NetworkConfig base;
  const char* argv[] = {"program", "--port", "19999"};
  auto result = wininspectd::apply_cli_overrides(base, 3, (char**)argv);
  DOCTEST_REQUIRE_EQ(result.port, 19999);
}

DOCTEST_TEST_CASE("config: apply_cli_overrides bind")
{
  NetworkConfig base;
  const char* argv[] = {"program", "--bind", "127.0.0.1"};
  auto result = wininspectd::apply_cli_overrides(base, 3, (char**)argv);
  DOCTEST_REQUIRE_EQ(result.bind.size(), 1u);
  DOCTEST_REQUIRE_EQ(result.bind[0].address, "127.0.0.1");
}

DOCTEST_TEST_CASE("config: apply_cli_overrides ipv4 flag")
{
  NetworkConfig base;
  const char* argv[] = {"program", "--ipv4"};
  auto result = wininspectd::apply_cli_overrides(base, 2, (char**)argv);
  // All bind addresses should be ipv4
  for (auto& addr : result.bind) {
    DOCTEST_REQUIRE_EQ(addr.family, ADDR_FAMILY_IPV4);
  }
}

DOCTEST_TEST_CASE("config: apply_cli_overrides instance name")
{
  NetworkConfig base;
  const char* argv[] = {"program", "--instance-name", "my-daemon"};
  auto result = wininspectd::apply_cli_overrides(base, 3, (char**)argv);
  DOCTEST_REQUIRE_EQ(result.identity.name, "my-daemon");
}

DOCTEST_TEST_CASE("config: apply_cli_overrides http port")
{
  NetworkConfig base;
  const char* argv[] = {"program", "--http-port", "8080"};
  auto result = wininspectd::apply_cli_overrides(base, 3, (char**)argv);
  DOCTEST_REQUIRE_EQ(result.http_port, 8080);
}

DOCTEST_TEST_CASE("config: apply_cli_overrides discovery port")
{
  NetworkConfig base;
  const char* argv[] = {"program", "--discovery-port", "1987"};
  auto result = wininspectd::apply_cli_overrides(base, 3, (char**)argv);
  DOCTEST_REQUIRE_EQ(result.discovery_port, 1987);
}

DOCTEST_TEST_CASE("config: apply_cli_overrides include hostname")
{
  NetworkConfig base;
  const char* argv[] = {"program", "--include-hostname"};
  auto result = wininspectd::apply_cli_overrides(base, 2, (char**)argv);
  DOCTEST_REQUIRE(result.include_hostname);
}

DOCTEST_TEST_CASE("config: apply_cli_overrides rate limit")
{
  NetworkConfig base;
  const char* argv[] = {"program", "--rate-limit-ms", "250"};
  auto result = wininspectd::apply_cli_overrides(base, 3, (char**)argv);
  DOCTEST_REQUIRE_EQ(result.rate_limit_ms, 250);
}

DOCTEST_TEST_CASE("config: apply_cli_overrides request timeout")
{
  NetworkConfig base;
  const char* argv[] = {"program", "--request-timeout", "10000"};
  auto result = wininspectd::apply_cli_overrides(base, 3, (char**)argv);
  DOCTEST_REQUIRE_EQ(result.request_timeout_ms, 10000);
}

DOCTEST_TEST_CASE("config: apply_cli_overrides rendezvous url")
{
  NetworkConfig base;
  const char* argv[] = {"program", "--rendezvous", "https://rv.example.com"};
  auto result = wininspectd::apply_cli_overrides(base, 3, (char**)argv);
  DOCTEST_REQUIRE_EQ(result.rendezvous.size(), 1u);
  DOCTEST_REQUIRE_EQ(result.rendezvous[0].url, "https://rv.example.com");
}
#endif
