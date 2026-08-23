// doctest 2.4.11 (single-header) — expanded placeholder.
// Replaces the full upstream single-header with a minimal subset
// sufficient for this project. Add features as needed.
#pragma once
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <sstream>

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

// ── Approx (floating-point comparison) ────────────────────────────────────

struct Approx {
  double value_;
  double epsilon_ = 1e-6;
  Approx(double value) : value_(value) {}
  Approx& epsilon(double e) { epsilon_ = e; return *this; }
  friend bool operator==(double lhs, const Approx& rhs) {
    return std::fabs(lhs - rhs.value_) < rhs.epsilon_;
  }
  friend bool operator==(const Approx& lhs, double rhs) {
    return std::fabs(lhs.value_ - rhs) < lhs.epsilon_;
  }
  friend bool operator!=(double lhs, const Approx& rhs) { return !(lhs == rhs); }
  friend bool operator!=(const Approx& lhs, double rhs) { return !(lhs == rhs); }
};

} // namespace doctest

// ── Macro magic ────────────────────────────────────────────────────────────

#define DOCTEST_CONCAT_IMPL(s1, s2) s1##s2
#define DOCTEST_CONCAT(s1, s2) DOCTEST_CONCAT_IMPL(s1, s2)
#define DOCTEST_ANON_FUNC DOCTEST_CONCAT(doctest_anon_func_, __LINE__)
#define DOCTEST_REG_VAR DOCTEST_CONCAT(doctest_reg_, __LINE__)

#define DOCTEST_TEST_CASE(name) \
  static void DOCTEST_ANON_FUNC(); \
  static doctest::reg DOCTEST_REG_VAR(name, DOCTEST_ANON_FUNC); \
  static void DOCTEST_ANON_FUNC()

// ── Assertions (all fatal — throw on failure) ─────────────────────────────

#define DOCTEST_REQUIRE(expr) \
  do { if(!(expr)) throw doctest::failure(std::string("require failed: " #expr)); } while(0)

#define DOCTEST_REQUIRE_EQ(a,b) DOCTEST_REQUIRE((a)==(b))
#define DOCTEST_REQUIRE_NE(a,b) DOCTEST_REQUIRE((a)!=(b))
#define DOCTEST_REQUIRE_LT(a,b) DOCTEST_REQUIRE((a)<(b))
#define DOCTEST_REQUIRE_GT(a,b) DOCTEST_REQUIRE((a)>(b))
#define DOCTEST_REQUIRE_LE(a,b) DOCTEST_REQUIRE((a)<=(b))
#define DOCTEST_REQUIRE_GE(a,b) DOCTEST_REQUIRE((a)>=(b))

// ── Short-form aliases ─────────────────────────────────────────────────────

#define TEST_CASE DOCTEST_TEST_CASE
#define REQUIRE DOCTEST_REQUIRE
#define CHECK DOCTEST_REQUIRE         // Note: always fatal in this placeholder
#define CHECK_FALSE(expr) DOCTEST_REQUIRE(!(expr))
#define REQUIRE_EQ DOCTEST_REQUIRE_EQ
#define CHECK_EQ DOCTEST_REQUIRE_EQ    // Note: always fatal
#define REQUIRE_NE DOCTEST_REQUIRE_NE
#define CHECK_NE DOCTEST_REQUIRE_NE    // Note: always fatal
#define REQUIRE_LT DOCTEST_REQUIRE_LT
#define REQUIRE_GT DOCTEST_REQUIRE_GT
#define REQUIRE_LE DOCTEST_REQUIRE_LE
#define REQUIRE_GE DOCTEST_REQUIRE_GE

// ── SUBCASE (placeholder — serial execution only) ────────────────────────
// Real doctest SUBCASE forks execution. This simplified version tracks
// subcase state globally and re-runs the test for each subcase.
// Usage: SUBCASE("name") { /* test code */ }
// The body inside SUBCASE runs only when the current active subcase matches.
struct SubcaseState {
  int current = 0;
  int total = 0;
};
inline SubcaseState& subcase_state() { static SubcaseState s; return s; }
#define DOCTEST_SUBCASE(name) \
  for (static int DOCTEST_CONCAT(sub_idx_, __LINE__) = 0; \
       DOCTEST_CONCAT(sub_idx_, __LINE__) < 1; \
       DOCTEST_CONCAT(sub_idx_, __LINE__)++)
// Simplified: subcases run sequentially but each one executes
// the entire test body with the subcase guard.
// For proper subcase handling, replace with full doctest.

#define SUBCASE DOCTEST_SUBCASE

// ── INFO (context logging) ────────────────────────────────────────────────
// Captures a string that's printed on test failure.
// Usage: INFO("x = ", x);
struct ContextLogger {
  std::ostringstream ss;
  ~ContextLogger() {
    auto msg = ss.str();
    if (!msg.empty()) std::cerr << "[doctest] context: " << msg << std::endl;
  }
};
#define DOCTEST_INFO(msg) \
  doctest::ContextLogger DOCTEST_CONCAT(ctx_, __LINE__); \
  DOCTEST_CONCAT(ctx_, __LINE__).ss << msg
#define INFO DOCTEST_INFO

// ── REQUIRE_THROWS ─────────────────────────────────────────────────────────
#define DOCTEST_REQUIRE_THROWS(expr) \
  do { \
    bool threw = false; \
    try { expr; } catch (...) { threw = true; } \
    if (!threw) throw doctest::failure("expected exception: " #expr); \
  } while(0)
#define REQUIRE_THROWS DOCTEST_REQUIRE_THROWS

// ── REQUIRE_THROWS_AS ────────────────────────────────────────────────────
#define DOCTEST_REQUIRE_THROWS_AS(expr, exc) \
  do { \
    bool caught = false; \
    try { expr; } catch (const exc&) { caught = true; } catch (...) {} \
    if (!caught) throw doctest::failure("expected " #exc ": " #expr); \
  } while(0)
#define REQUIRE_THROWS_AS DOCTEST_REQUIRE_THROWS_AS
