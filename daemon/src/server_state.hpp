#pragma once
#include <mutex>
#include <map>
#include <list>
#include <string>
#include <chrono>
#include <atomic>
#include "wininspect/types.hpp"

namespace wininspect {

struct ServerState {
  std::mutex mu;
  std::uint64_t snap_counter = 1;
  std::map<std::string, Snapshot> snaps;
  std::map<std::string, int> pinned_counts;
  std::list<std::string> lru_order; // LRU: front is oldest, back is newest
  
  // Configurable limits
  size_t max_snapshots = 1000;
  int max_connections = 32;
  int session_ttl_sec = 3600; // 1 hour default
  std::atomic<int> active_connections{0};

  // Temporal limits
  int request_timeout_ms = 5000; // 5s watchdog
  int poll_interval_ms = 100;
  int max_wait_ms = 30000; // 30s max for long polls
  int discovery_port = 1986; // Discovery UDP port

  struct PersistentSession {
    std::string last_snap_id;
    bool subscribed = false;
    std::chrono::steady_clock::time_point last_activity;
  };
  std::map<std::string, PersistentSession> sessions;
};

struct ClientSession {
  SessionID id;
  bool authenticated = false;
  std::string last_snap_id;
  bool subscribed = false;
};

} // namespace wininspect
