#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include <mutex>
#include <map>
#include <list>
#include <string>
#include <chrono>
#include <atomic>
#include <thread>
#include <vector>
#include <set>
#include <memory>
#include "wininspect/types.hpp"
#include "wininspect/network_config.hpp"

namespace wininspect {

struct ServerState {
  std::mutex snapshots_mu;   // protects snaps, pinned_counts, lru_order, snap_counter
  std::mutex sessions_mu;    // protects sessions, sessionCount, event_counter, event_log
  std::uint64_t snap_counter = 1;
  std::map<std::string, std::shared_ptr<Snapshot>> snaps;
  std::map<std::string, int> pinned_counts;
  std::list<std::string> lru_order; // LRU: front is oldest, back is newest

  // Client thread tracking (joined on shutdown via jthread auto-join)
  std::vector<std::jthread> client_threads;
  std::mutex thread_mu; // protects client_threads

  // Event Sequencing
  std::atomic<std::uint64_t> event_counter{1};
  std::vector<Event> event_log;
  size_t max_event_log = 1000;

  // Configurable limits
  size_t max_snapshots = 1000;
  size_t max_sessions = 256;
  size_t max_mem_read_size = 1024 * 1024; // 1MB default
  int uia_depth = 5;
  int service_timeout_sec = 30;
  int max_connections = 32;
  int session_ttl_sec = 3600; // 1 hour default
  std::atomic<int> active_connections{0};

  // Temporal limits
  int request_timeout_ms = 5000; // 5s watchdog
  int poll_interval_ms = 100;
  int max_wait_ms = 30000; // 30s max for long polls
  int discovery_port = 1986; // Discovery UDP port
  int rate_limit_ms = 0;
  std::chrono::steady_clock::time_point last_accept_time;

  struct PersistentSession {
    std::string last_snap_id;
    bool subscribed = false;
    std::chrono::steady_clock::time_point last_activity;
  };
  std::map<std::string, PersistentSession> sessions;
  // Method authorization sets
  std::set<std::string> allow_methods;  // empty = all allowed
  std::set<std::string> deny_methods;   // empty = none denied

  // Network configuration (loaded at startup)
  NetworkConfig net_config;
};

struct ClientSession {
  SessionID id;
  bool authenticated = false;
  std::string last_snap_id;
  bool subscribed = false;
};

} // namespace wininspect
