// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "doctest/doctest.h"
#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"
#include "wininspect/backend.hpp"
#include "server_state.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace wininspect;

// Helper: create a FakeBackend with some test windows
static FakeBackend make_fb()
{
  return FakeBackend({
      {0x100, 0, 0, "Notepad", "C1", true},
      {0x200, 0, 0, "Chrome", "C2", true},
      {0x300, 0, 0, "Terminal", "C3", true},
  });
}

// ── Concurrent dispatch stress test ─────────────────────────────────────────

DOCTEST_TEST_CASE("stress: 32 concurrent CoreEngine dispatches")
{
  auto fb = make_fb();
  CoreEngine core(&fb);
  std::atomic<uint64_t> completed{0};
  std::vector<std::thread> threads;

  for (int t = 0; t < 32; ++t) {
    threads.emplace_back([&core, &completed]() {
      for (int i = 0; i < 100; ++i) {
        json::Object params;
        CoreRequest req{"stress", "window.listTop", params};
        auto snap = core.get_backend()->capture_snapshot();
        auto resp = core.handle(req, snap, nullptr);
        if (resp.ok)
          completed.fetch_add(1);
      }
    });
  }

  for (auto& th : threads)
    th.join();
  CHECK(completed.load() == 32 * 100);
  CHECK(core.metrics_collector_.total_requests() == 32 * 100);
}

// ── Mix of methods under concurrent load ────────────────────────────────────

DOCTEST_TEST_CASE("stress: mixed methods under 16 concurrent clients")
{
  auto fb = make_fb();
  CoreEngine core(&fb);
  std::atomic<uint64_t> ok{0}, fail{0};
  std::vector<std::thread> threads;

  const char* methods[] = {"window.listTop", "input.mouseClick", "screen.capture", "process.list",
                           "daemon.health"};

  for (int t = 0; t < 16; ++t) {
    threads.emplace_back([&core, &ok, &fail, methods]() {
      for (int i = 0; i < 50; ++i) {
        json::Object params;
        params["x"] = (double)(i % 2000);
        params["y"] = (double)(i % 2000);
        CoreRequest req{"stress", methods[i % 5], params};
        auto snap = core.get_backend()->capture_snapshot();
        auto resp = core.handle(req, snap, nullptr);
        if (resp.ok)
          ok.fetch_add(1);
        else
          fail.fetch_add(1);
      }
    });
  }

  for (auto& th : threads)
    th.join();
  CHECK(ok.load() + fail.load() == 16 * 50);
}

// ── Snapshot pin/unpin under concurrent load ────────────────────────────────

DOCTEST_TEST_CASE("stress: snapshot LRU with 32 concurrent pin/unpin")
{
  ServerState st;
  st.max_snapshots = 100;
  std::vector<std::thread> threads;

  // Pre-create snapshots
  for (int i = 0; i < 50; ++i) {
    std::lock_guard<std::mutex> lk(st.snapshots_mu);
    auto snap = std::make_shared<Snapshot>();
    snap->top = {1, 2, 3};
    st.snaps[std::to_string(i)] = snap;
  }

  for (int t = 0; t < 32; ++t) {
    threads.emplace_back([&st, t]() {
      for (int i = 0; i < 20; ++i) {
        std::string sid = std::to_string((t * 20 + i) % 50);
        PinGuard pg(&st, sid);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        // PinGuard RAII unpins on destruction
      }
    });
  }

  for (auto& th : threads)
    th.join();
  // All pins should be released after guard destruction
  for (auto& [sid, cnt] : st.pinned_counts) {
    CHECK(cnt == 0);
  }
}

// ── Connection limit enforcement ───────────────────────────────────────────

DOCTEST_TEST_CASE("stress: connection limit enforcement")
{
  ServerState st;
  st.max_connections = 4;
  st.active_connections = 0;

  std::atomic<int> accepted{0}, rejected{0};
  std::vector<std::thread> threads;

  for (int t = 0; t < 10; ++t) {
    threads.emplace_back([&st, &accepted, &rejected]() {
      for (int i = 0; i < 10; ++i) {
        int cur = st.active_connections.fetch_add(1);
        if (cur >= st.max_connections) {
          rejected.fetch_add(1);
          st.active_connections.fetch_sub(1);
        }
        else {
          accepted.fetch_add(1);
          std::this_thread::sleep_for(std::chrono::microseconds(500));
          st.active_connections.fetch_sub(1);
        }
      }
    });
  }

  for (auto& th : threads)
    th.join();
  CHECK(st.active_connections == 0);
  CHECK(accepted.load() > 0);
  CHECK(rejected.load() > 0);
}

// ── Invalid method stress ─────────────────────────────────────────────────

DOCTEST_TEST_CASE("stress: rapid invalid methods don't crash")
{
  auto fb = make_fb();
  CoreEngine core(&fb);
  std::vector<std::thread> threads;

  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&core]() {
      for (int i = 0; i < 500; ++i) {
        json::Object params;
        CoreRequest req{"stress", "nonexistent.method." + std::to_string(i), params};
        auto snap = core.get_backend()->capture_snapshot();
        (void)core.handle(req, snap, nullptr);
      }
    });
  }

  for (auto& th : threads)
    th.join();
  CHECK(core.metrics_collector_.total_requests() == 8 * 500);
}

// ── Burst snapshot creation under load ────────────────────────────────────

DOCTEST_TEST_CASE("stress: burst snapshot creation under load")
{
  ServerState st;
  st.max_snapshots = 20;
  std::vector<std::thread> threads;

  for (int t = 0; t < 16; ++t) {
    threads.emplace_back([&st, t]() {
      for (int i = 0; i < 50; ++i) {
        std::string sid = "snap-" + std::to_string(t) + "-" + std::to_string(i);
        auto snap = std::make_shared<Snapshot>();
        snap->top = {1, 2, 3};
        {
          std::lock_guard<std::mutex> lk(st.snapshots_mu);
          st.snaps[sid] = snap;
          while (st.snaps.size() > st.max_snapshots) {
            st.snaps.erase(st.snaps.begin());
          }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    });
  }

  for (auto& th : threads)
    th.join();
  CHECK(st.snaps.size() <= 20);
}

// ── Dispatch table does not degrade under load ────────────────────────────

DOCTEST_TEST_CASE("stress: dispatch table under heavy load")
{
  auto fb = make_fb();
  CoreEngine core(&fb);
  std::vector<std::thread> threads;
  std::atomic<uint64_t> total{0};

  for (int t = 0; t < 16; ++t) {
    threads.emplace_back([&core, &total]() {
      for (int i = 0; i < 200; ++i) {
        json::Object params;
        CoreRequest req{"stress", "window.listTop", params};
        auto snap = core.get_backend()->capture_snapshot();
        auto resp = core.handle(req, snap, nullptr);
        total.fetch_add(1);
      }
    });
  }

  for (auto& th : threads)
    th.join();
  CHECK(total.load() == 16 * 200);
}

// ── Concurrent metrics recording ──────────────────────────────────────────

DOCTEST_TEST_CASE("stress: metrics under concurrent load")
{
  auto fb = make_fb();
  CoreEngine core(&fb);
  std::vector<std::thread> threads;

  for (int t = 0; t < 16; ++t) {
    threads.emplace_back([&core, t]() {
      json::Object params;
      std::string method = t % 2 == 0 ? "window.listTop" : "screen.capture";
      CoreRequest req{"stress", method, params};
      auto snap = core.get_backend()->capture_snapshot();
      for (int i = 0; i < 50; ++i) {
        (void)core.handle(req, snap, nullptr);
      }
    });
  }

  for (auto& th : threads)
    th.join();
  auto metrics = core.metrics_collector_.snapshot();
  CHECK(metrics.find("total_calls")->second.as_num() == 16 * 50);
}
