// doctest 2.4.11 (single-header) - trimmed placeholder for scaffold.
// For full doctest, replace this file with upstream single-header.
// This placeholder provides a minimal subset sufficient for compilation in this scaffold.
#pragma once
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace doctest {
struct TestCase { const char* name; std::function<void()> fn; };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }

struct failure : std::exception {
  std::string msg;
  explicit failure(std::string m): msg(std::move(m)) {}
  const char* what() const noexcept override { return msg.c_str(); }
};

struct reg {
  reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline int run_all() {
  int fails = 0;
  for (auto& tc : registry()) {
    try { tc.fn(); }
    catch (const std::exception& e) {
      ++fails;
      std::cerr << "[doctest] FAIL: " << tc.name << " :: " << e.what() << "\n";
    }
  }
  if (fails == 0) std::cerr << "[doctest] OK (" << registry().size() << " tests)\n";
  return fails == 0 ? 0 : 1;
}
} // namespace doctest

#define DOCTEST_CONCAT_IMPL(s1, s2) s1##s2
#define DOCTEST_CONCAT(s1, s2) DOCTEST_CONCAT_IMPL(s1, s2)
#define DOCTEST_ANON_FUNC DOCTEST_CONCAT(doctest_anon_func_, __LINE__)
#define DOCTEST_REG_VAR DOCTEST_CONCAT(doctest_reg_, __LINE__)

#define DOCTEST_TEST_CASE(name) \
  static void DOCTEST_ANON_FUNC(); \
  static doctest::reg DOCTEST_REG_VAR(name, DOCTEST_ANON_FUNC); \
  static void DOCTEST_ANON_FUNC()

#define DOCTEST_REQUIRE(expr) \
  do { if(!(expr)) throw doctest::failure(std::string("require failed: " #expr)); } while(0)

#define DOCTEST_REQUIRE_EQ(a,b) DOCTEST_REQUIRE((a)==(b))
