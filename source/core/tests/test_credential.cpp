// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// Credential manager tests. Tests store/retrieve/remove/redact cycle.
// Uses the DPAPI fallback path (file-based) which works in both MSVC and
// MinGW/Wine environments. The key material never leaves the local machine.

#include "doctest/doctest.h"
#include "wininspect/credential_manager.hpp"
#include "wininspect/base64.hpp"

#ifdef _WIN32
using namespace wininspect;

DOCTEST_TEST_CASE("credential: store and retrieve")
{
  CredentialManager cm;
  cm.force_dpapi();
  std::vector<uint8_t> blob = {0x01, 0x02, 0x03, 0x04};
  DOCTEST_REQUIRE(cm.store("test-key-1", "test-user", "ssh-key", blob));
  auto result = cm.retrieve("test-key-1");
  DOCTEST_REQUIRE(result.has_value());
  DOCTEST_REQUIRE_EQ(result->target, "test-key-1");
  DOCTEST_REQUIRE_EQ(result->username, "test-user");
  DOCTEST_REQUIRE(result->blob.size() == blob.size());
  DOCTEST_REQUIRE(memcmp(result->blob.data(), blob.data(), blob.size()) == 0);
  // Cleanup
  cm.remove("test-key-1");
}

DOCTEST_TEST_CASE("credential: remove")
{
  CredentialManager cm;
  cm.force_dpapi();
  std::vector<uint8_t> blob = {'t', 'e', 's', 't'};
  cm.store("test-remove", "u", "password", blob);
  DOCTEST_REQUIRE(cm.remove("test-remove"));
  auto result = cm.retrieve("test-remove");
  DOCTEST_REQUIRE(!result.has_value());
}

DOCTEST_TEST_CASE("credential: retrieve nonexistent returns nullopt")
{
  CredentialManager cm;
  cm.force_dpapi();
  auto result = cm.retrieve("nonexistent-key");
  DOCTEST_REQUIRE(!result.has_value());
}

DOCTEST_TEST_CASE("credential: list returns stored targets")
{
  CredentialManager cm;
  cm.force_dpapi();
  std::vector<uint8_t> b = {'d', 'a', 't', 'a'};
  cm.store("list-key-1", "u1", "ssh-key", b);
  cm.store("list-key-2", "u2", "password", b);
  auto entries = cm.list();
  DOCTEST_REQUIRE(entries.size() >= 2);
  bool found1 = false, found2 = false;
  for (auto& e : entries) {
    if (e.target == "list-key-1")
      found1 = true;
    if (e.target == "list-key-2")
      found2 = true;
    // Listed entries should NOT contain blob data
    DOCTEST_REQUIRE(e.blob.empty());
  }
  DOCTEST_REQUIRE(found1);
  DOCTEST_REQUIRE(found2);
  cm.remove("list-key-1");
  cm.remove("list-key-2");
}

DOCTEST_TEST_CASE("credential: overwrite existing")
{
  CredentialManager cm;
  cm.force_dpapi();
  std::vector<uint8_t> b1 = {'o', 'l', 'd'};
  std::vector<uint8_t> b2 = {'n', 'e', 'w'};
  cm.store("overwrite-test", "u", "password", b1);
  cm.store("overwrite-test", "u", "password", b2);
  auto result = cm.retrieve("overwrite-test");
  DOCTEST_REQUIRE(result.has_value());
  DOCTEST_REQUIRE(memcmp(result->blob.data(), b2.data(), b2.size()) == 0);
  cm.remove("overwrite-test");
}

DOCTEST_TEST_CASE("credential: redact filters target names")
{
  CredentialManager cm;
  cm.force_dpapi();
  std::vector<uint8_t> b = {'x'};
  cm.store("secret-api-key", "admin", "api-token", b);
  std::string log_msg = "Processing credential secret-api-key for auth";
  std::string redacted = cm.redact(log_msg);
  DOCTEST_REQUIRE(redacted.find("secret-api-key") == std::string::npos);
  DOCTEST_REQUIRE(redacted.find("<REDACTED>") != std::string::npos);
  cm.remove("secret-api-key");
}

DOCTEST_TEST_CASE("credential: generate password length and charset")
{
  auto pwd = CredentialManager::generate_password(32);
  DOCTEST_REQUIRE_EQ(pwd.size(), 32u);
  // Verify it contains characters from different categories
  bool has_upper = false, has_lower = false, has_digit = false, has_special = false;
  for (char c : pwd) {
    if (c >= 'A' && c <= 'Z')
      has_upper = true;
    else if (c >= 'a' && c <= 'z')
      has_lower = true;
    else if (c >= '0' && c <= '9')
      has_digit = true;
    else
      has_special = true;
  }
  DOCTEST_REQUIRE(has_upper);
  DOCTEST_REQUIRE(has_lower);
  DOCTEST_REQUIRE(has_digit);
  // Use default length
  auto pwd2 = CredentialManager::generate_password();
  DOCTEST_REQUIRE_EQ(pwd2.size(), 32u);
  // Two generated passwords should differ
  DOCTEST_REQUIRE(pwd != pwd2);
}
#endif
