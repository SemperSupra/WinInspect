// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/metrics.hpp"
#include <cmath>

namespace wininspect {

  static constexpr size_t MAX_SAMPLES = 1000;

  void MetricsCollector::record(const std::string& method, double duration_ms, bool success)
  {
    auto& m = methods_[method]; // map insertion may allocate (rare after warmup)

    // Lock-free counter updates (hot path — no mutex)
    m.calls.fetch_add(1, std::memory_order_relaxed);
    if (success)
      m.ok.fetch_add(1, std::memory_order_relaxed);
    m.total_ms.fetch_add(duration_ms, std::memory_order_relaxed);

    // Min/max with relaxed atomic compare-exchange
    double prev_min = m.min_ms.load(std::memory_order_relaxed);
    while (duration_ms < prev_min) {
      if (m.min_ms.compare_exchange_weak(prev_min, duration_ms, std::memory_order_relaxed))
        break;
    }
    double prev_max = m.max_ms.load(std::memory_order_relaxed);
    while (duration_ms > prev_max) {
      if (m.max_ms.compare_exchange_weak(prev_max, duration_ms, std::memory_order_relaxed))
        break;
    }

    // P99 deque still needs the mutex (only every MAX_SAMPLES calls)
    auto calls = m.calls.load(std::memory_order_relaxed);
    if (calls <= MAX_SAMPLES || calls % (MAX_SAMPLES / 10) == 0) {
      std::lock_guard<std::mutex> lk(mu_);
      m.recent.push_back(duration_ms);
      if (m.recent.size() > MAX_SAMPLES)
        m.recent.pop_front();
    }
  }

  json::Object MetricsCollector::snapshot() const
  {
    std::lock_guard<std::mutex> lk(mu_);
    json::Object result;
    json::Array arr;
    for (auto& [method, pm] : methods_) {
      json::Object o;
      o["method"] = method;
      o["calls"] = (double)pm.calls.load(std::memory_order_relaxed);
      o["ok"] = (double)pm.ok.load(std::memory_order_relaxed);
      auto total_calls = pm.calls.load(std::memory_order_relaxed);
      auto total_ok = pm.ok.load(std::memory_order_relaxed);
      o["errors"] = (double)(total_calls - total_ok);
      auto tms = pm.total_ms.load(std::memory_order_relaxed);
      o["avg_ms"] = total_calls > 0 ? tms / total_calls : 0.0;
      o["min_ms"] = pm.min_ms.load(std::memory_order_relaxed);
      o["max_ms"] = pm.max_ms.load(std::memory_order_relaxed);

      // Compute P99 from recent samples
      if (!pm.recent.empty()) {
        auto sorted = pm.recent;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = (size_t)std::ceil(0.99 * sorted.size()) - 1;
        o["p99_ms"] = sorted[std::min(idx, sorted.size() - 1)];
      }
      else {
        o["p99_ms"] = 0.0;
      }
      arr.push_back(o);
    }
    result["methods"] = arr;
    result["total_methods"] = (double)methods_.size();

    uint64_t total = 0;
    for (auto& [_, pm] : methods_)
      total += pm.calls.load(std::memory_order_relaxed);
    result["total_calls"] = (double)total;
    return result;
  }

  void MetricsCollector::reset()
  {
    std::lock_guard<std::mutex> lk(mu_);
    methods_.clear();
  }

  std::string MetricsCollector::to_prometheus(const std::string& instance_id,
                                              int active_connections,
                                              const std::string& controller) const
  {
    std::lock_guard<std::mutex> lk(mu_);
    std::string out;
    out.reserve(4096);

    // HELP / TYPE headers
    out += "# HELP wininspect_rpc_total Total RPC calls by method.\n";
    out += "# TYPE wininspect_rpc_total counter\n";
    out += "# HELP wininspect_rpc_errors_total RPC errors by method.\n";
    out += "# TYPE wininspect_rpc_errors_total counter\n";
    out += "# HELP wininspect_rpc_duration_ms RPC latency quantiles by method.\n";
    out += "# TYPE wininspect_rpc_duration_ms gauge\n";
    out += "# HELP wininspect_connections_active Current active connections.\n";
    out += "# TYPE wininspect_connections_active gauge\n";
    out += "# HELP wininspect_control_state Current controller (1=enabled, 0=disabled).\n";
    out += "# TYPE wininspect_control_state gauge\n";
    out += "# HELP wininspect_build_info Build metadata.\n";
    out += "# TYPE wininspect_build_info gauge\n";

    // Constant build info
    out += "wininspect_build_info{version=\"0.4.0\"";
    if (!instance_id.empty())
      out += ",instance=\"" + instance_id + "\"";
    out += "} 1\n";

    // Per-method metrics
    for (auto& [method, pm] : methods_) {
      auto calls = pm.calls.load(std::memory_order_relaxed);
      auto ok = pm.ok.load(std::memory_order_relaxed);
      auto errors = calls - ok;
      auto tms = pm.total_ms.load(std::memory_order_relaxed);
      double avg = calls > 0 ? tms / calls : 0.0;
      double p99 = 0.0;
      if (!pm.recent.empty()) {
        auto sorted = pm.recent;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = (size_t)std::ceil(0.99 * sorted.size()) - 1;
        p99 = sorted[std::min(idx, sorted.size() - 1)];
      }

      out += "wininspect_rpc_total{method=\"" + method + "\"} " + std::to_string(calls) + "\n";
      out +=
          "wininspect_rpc_errors_total{method=\"" + method + "\"} " + std::to_string(errors) + "\n";
      out += "wininspect_rpc_duration_ms{method=\"" + method + "\",quantile=\"avg\"} " +
             std::to_string(avg) + "\n";
      out += "wininspect_rpc_duration_ms{method=\"" + method + "\",quantile=\"p99\"} " +
             std::to_string(p99) + "\n";
      out += "wininspect_rpc_duration_ms{method=\"" + method + "\",quantile=\"min\"} " +
             std::to_string(pm.min_ms.load(std::memory_order_relaxed)) + "\n";
      out += "wininspect_rpc_duration_ms{method=\"" + method + "\",quantile=\"max\"} " +
             std::to_string(pm.max_ms.load(std::memory_order_relaxed)) + "\n";
    }

    // Server-state metrics
    out += "wininspect_connections_active " + std::to_string(active_connections) + "\n";

    out += "wininspect_control_state{controller=\"" + controller + "\"} 1\n";
    if (controller != "none")
      out += "wininspect_control_state{controller=\"none\"} 0\n";

    return out;
  }

  uint64_t MetricsCollector::total_requests() const
  {
    uint64_t total = 0;
    for (auto& [_, pm] : methods_)
      total += pm.calls.load(std::memory_order_relaxed);
    return total;
  }

} // namespace wininspect
