#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// Local-only performance metrics collector. Zero telemetry — data stays
// on the machine. Tracks per-method call count, total time, min, max,
// and P99 latency. Access via daemon.metrics RPC or wininspect metrics CLI.
//
// Hot path (record) is lock-free for counters; mutex only for P99 deque.

#include "tinyjson.hpp"
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <deque>
#include <algorithm>

namespace wininspect {

  class MetricsCollector
  {
  public:
    MetricsCollector() = default;

    /// Record one method invocation with its duration in milliseconds.
    void record(const std::string& method, double duration_ms, bool ok);

    /// Return all metrics as a JSON object (calls, avg_ms, p99_ms, min_ms, max_ms).
    json::Object snapshot() const;

    /// Return all metrics as Prometheus exposition-format text.
    /// https://prometheus.io/docs/instrumenting/exposition_formats/
    std::string to_prometheus(const std::string& instance_id = "", int active_connections = 0,
                              const std::string& controller = "none") const;

    /// Reset all counters to zero.
    void reset();

    /// Total number of requests recorded across all methods.
    uint64_t total_requests() const;

  private:
    struct PerMethod
    {
      std::atomic<uint64_t> calls{0};
      std::atomic<uint64_t> ok{0};
      std::atomic<double> total_ms{0.0};
      std::atomic<double> min_ms{1e9};
      std::atomic<double> max_ms{0.0};
      std::deque<double> recent; // last 1000 samples for P99 (under mu_)
    };

    mutable std::mutex mu_;
    std::map<std::string, PerMethod> methods_;
  };

} // namespace wininspect
