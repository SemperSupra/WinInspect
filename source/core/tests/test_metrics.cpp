// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "doctest/doctest.h"
#include "wininspect/metrics.hpp"
#include <thread>

using namespace wininspect;

/// Helper: find a method in the snapshot array by name.
static json::Object find_method(const json::Object& snap, const std::string& name)
{
  auto it = snap.find("methods");
  if (it == snap.end())
    return {};
  auto& arr = it->second.as_arr();
  for (auto& v : arr) {
    if (v.as_obj().find("method")->second.as_str() == name)
      return v.as_obj();
  }
  return {};
}

// ── Basic recording and snapshot ────────────────────────────────────────────

DOCTEST_TEST_CASE("MetricsCollector records and snapshots a single method")
{
  MetricsCollector mc;
  mc.record("test.method", 10.5, true);

  auto snap = mc.snapshot();
  CHECK(snap.find("total_calls")->second.as_num() == 1.0);
  CHECK(snap.find("total_methods")->second.as_num() == 1.0);

  auto m = find_method(snap, "test.method");
  CHECK(m["calls"].as_num() == 1.0);
  CHECK(m["avg_ms"].as_num() == 10.5);
  CHECK(m["min_ms"].as_num() == 10.5);
  CHECK(m["max_ms"].as_num() == 10.5);
}

DOCTEST_TEST_CASE("MetricsCollector aggregates multiple calls")
{
  MetricsCollector mc;
  mc.record("agg", 10.0, true);
  mc.record("agg", 20.0, true);
  mc.record("agg", 30.0, true);

  auto m = find_method(mc.snapshot(), "agg");
  CHECK(m["calls"].as_num() == 3.0);
  CHECK(m["avg_ms"].as_num() == 20.0);
  CHECK(m["min_ms"].as_num() == 10.0);
  CHECK(m["max_ms"].as_num() == 30.0);
}

// ── P99 computation ────────────────────────────────────────────────────────

DOCTEST_TEST_CASE("MetricsCollector P99 computation")
{
  MetricsCollector mc;
  for (int i = 0; i < 999; ++i)
    mc.record("p99test", 1.0, true);
  mc.record("p99test", 100.0, true);

  double p99 = find_method(mc.snapshot(), "p99test")["p99_ms"].as_num();
  CHECK(p99 <= 2.0);
}

DOCTEST_TEST_CASE("MetricsCollector P99 with few samples returns max")
{
  MetricsCollector mc;
  mc.record("few", 5.0, true);
  mc.record("few", 10.0, true);

  double p99 = find_method(mc.snapshot(), "few")["p99_ms"].as_num();
  CHECK(p99 == 10.0);
}

// ── Reset ──────────────────────────────────────────────────────────────────

DOCTEST_TEST_CASE("MetricsCollector reset clears all data")
{
  MetricsCollector mc;
  mc.record("a", 1.0, true);
  CHECK(mc.total_requests() == 1);

  mc.reset();
  CHECK(mc.total_requests() == 0);
  CHECK(mc.snapshot().find("total_methods")->second.as_num() == 0.0);
}

// ── Total requests ─────────────────────────────────────────────────────────

DOCTEST_TEST_CASE("MetricsCollector total_requests")
{
  MetricsCollector mc;
  CHECK(mc.total_requests() == 0);

  mc.record("x", 1.0, true);
  mc.record("y", 2.0, false);
  mc.record("z", 3.0, true);

  CHECK(mc.total_requests() == 3);
}

// ── Multi-method isolation ────────────────────────────────────────────────

DOCTEST_TEST_CASE("MetricsCollector methods are independent")
{
  MetricsCollector mc;
  mc.record("alpha", 100.0, true);
  mc.record("beta", 200.0, true);

  auto snap = mc.snapshot();
  CHECK(find_method(snap, "alpha")["avg_ms"].as_num() == 100.0);
  CHECK(find_method(snap, "beta")["avg_ms"].as_num() == 200.0);
}

// ── Concurrent recording ──────────────────────────────────────────────────

DOCTEST_TEST_CASE("MetricsCollector concurrent recording")
{
  MetricsCollector mc;
  std::thread t1([&]() {
    for (int i = 0; i < 100; ++i)
      mc.record("concurrent", 1.0, true);
  });
  std::thread t2([&]() {
    for (int i = 0; i < 100; ++i)
      mc.record("concurrent", 1.0, true);
  });
  t1.join();
  t2.join();

  CHECK(mc.total_requests() == 200);
  CHECK(find_method(mc.snapshot(), "concurrent")["calls"].as_num() == 200.0);
}

// ── OK/error tracking ──────────────────────────────────────────────────────

DOCTEST_TEST_CASE("MetricsCollector tracks ok vs error counts")
{
  MetricsCollector mc;
  mc.record("good", 1.0, true);
  mc.record("good", 2.0, true);
  mc.record("bad", 1.0, false);

  auto snap = mc.snapshot();
  CHECK(find_method(snap, "good")["ok"].as_num() == 2.0);
  CHECK(find_method(snap, "good")["errors"].as_num() == 0.0);
  CHECK(find_method(snap, "bad")["ok"].as_num() == 0.0);
  CHECK(find_method(snap, "bad")["errors"].as_num() == 1.0);
}

// ── Empty snapshot ─────────────────────────────────────────────────────────

DOCTEST_TEST_CASE("MetricsCollector empty snapshot has zero totals")
{
  MetricsCollector mc;
  auto snap = mc.snapshot();
  CHECK(snap.find("total_calls")->second.as_num() == 0.0);
  CHECK(snap.find("total_methods")->second.as_num() == 0.0);
  CHECK(snap.find("methods")->second.as_arr().empty());
}
