// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung
//
// Benchmarks for hot-path functions: base64, JSON, compression.
// Reports timing in microseconds. Run: ./benchmarks

#include "wininspect/base64.hpp"
#include "wininspect/tinyjson.hpp"
#include "wininspect/compress.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <cstring>
#include <functional>

// ── Helpers ─────────────────────────────────────────────────────────────────

static std::vector<uint8_t> random_bytes(size_t n)
{
  std::vector<uint8_t> v(n);
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> dist(0, 255);
  for (auto& b : v)
    b = (uint8_t)dist(rng);
  return v;
}

struct BenchResult
{
  const char* name;
  int iterations;
  double avg_us;
};

static BenchResult bench(const char* name, int iterations, std::function<void()> fn)
{
  fn(); // warmup
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < iterations; i++)
    fn();
  auto end = std::chrono::steady_clock::now();
  auto total = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  return {name, iterations, (double)total / iterations};
}

// ── Benchmark 1: base64 encode 1KB ─────────────────────────────────────────
static BenchResult bench_b64_encode_1k()
{
  auto data = random_bytes(1024);
  return bench("base64::encode 1KB", 10000, [&]() { (void)wininspect::base64::encode(data); });
}

// ── Benchmark 2: base64 decode 1KB ─────────────────────────────────────────
static BenchResult bench_b64_decode_1k()
{
  auto data = random_bytes(1024);
  auto encoded = wininspect::base64::encode(data);
  return bench("base64::decode 1KB", 10000, [&]() { (void)wininspect::base64::decode(encoded); });
}

// ── Benchmark 3: base64 encode 6MB ─────────────────────────────────────────
static BenchResult bench_b64_encode_6mb()
{
  auto data = random_bytes(6 * 1024 * 1024);
  return bench("base64::encode 6MB", 10, [&]() { (void)wininspect::base64::encode(data); });
}

// ── Benchmark 4: JSON parse small request ───────────────────────────────────
static BenchResult bench_json_parse_small()
{
  std::string json = R"({"id":"test-1","method":"window.listTop","params":{"canonical":true}})";
  return bench("json::parse small", 50000, [&]() { (void)wininspect::json::parse(json); });
}

// ── Benchmark 5: JSON dumps small ───────────────────────────────────────────
static BenchResult bench_json_dumps_small()
{
  return bench("json::dumps small", 50000, [&]() {
    wininspect::json::Object o;
    o["ok"] = true;
    o["id"] = std::string("test-1");
    wininspect::json::Object r;
    r["hwnd"] = std::string("0x1234");
    r["title"] = std::string("TestWindow");
    o["result"] = r;
    (void)wininspect::json::dumps(o);
  });
}

// ── Benchmark 6: JSON parse 100 windows ────────────────────────────────────
static BenchResult bench_json_parse_100w()
{
  wininspect::json::Object big;
  big["ok"] = true;
  wininspect::json::Array arr;
  for (int i = 0; i < 100; i++) {
    wininspect::json::Object w;
    w["hwnd"] = std::string("0x") + std::to_string(i);
    w["title"] = std::string("Window #") + std::to_string(i);
    w["class_name"] = std::string("Notepad");
    arr.push_back(w);
  }
  big["result"] = arr;
  auto json = wininspect::json::dumps(big);
  return bench("json::parse 100 windows", 10000, [&]() { (void)wininspect::json::parse(json); });
}

// ── Benchmark 7: JSON dumps 100 windows ────────────────────────────────────
static BenchResult bench_json_dumps_100w()
{
  return bench("json::dumps 100 windows", 10000, [&]() {
    wininspect::json::Object o;
    o["ok"] = true;
    wininspect::json::Array arr;
    for (int i = 0; i < 100; i++) {
      wininspect::json::Object w;
      w["hwnd"] = std::string("0x") + std::to_string(i);
      w["title"] = std::string("Window #") + std::to_string(i);
      arr.push_back(w);
    }
    o["result"] = arr;
    (void)wininspect::json::dumps(o);
  });
}

// ── Benchmark 8: compress 6MB random (incompressible) ──────────────────────
static BenchResult bench_compress_6mb()
{
  auto data = random_bytes(6 * 1024 * 1024);
  return bench("compress 6MB random", 5, [&]() { (void)wininspect::compress(data); });
}

// ── Benchmark 9: simulated round-trip (snapshot+listTop) ────────────────────
static BenchResult bench_roundtrip()
{
  return bench("simulated snapshot+listTop", 1000, [&]() {
    wininspect::json::Object resp;
    resp["ok"] = true;
    resp["id"] = std::string("test-1");
    wininspect::json::Array wins;
    for (int i = 0; i < 20; i++) {
      wininspect::json::Object w;
      w["hwnd"] = std::string("0x") + std::to_string(i);
      wins.push_back(w);
    }
    resp["result"] = wins;
    (void)wininspect::json::dumps(resp);
  });
}

// ── Main ────────────────────────────────────────────────────────────────────

int main()
{
  std::cout << "=== WinInspect Benchmarks ===\n\n";

  BenchResult results[] = {
      bench_b64_encode_1k(),    bench_b64_decode_1k(),    bench_b64_encode_6mb(),
      bench_json_parse_small(), bench_json_dumps_small(), bench_json_parse_100w(),
      bench_json_dumps_100w(),  bench_compress_6mb(),     bench_roundtrip(),
  };

  std::cout << "Benchmark                           Iterations    Avg (us)\n";
  std::cout << "------------------------------------------------------------\n";
  for (auto& r : results) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%-35s %8d  %10.1f", r.name, r.iterations, r.avg_us);
    std::cout << buf << "\n";
  }

  std::cout << "\n=== Benchmarks complete ===\n";
  return 0;
}
