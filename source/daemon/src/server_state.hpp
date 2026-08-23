#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include <mutex>
#include <map>
#include <set>
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
#include "control_manager.hpp"

// Thread handle with completion flag for safe lifecycle tracking.
struct ThreadHandle
{
  std::thread t;
  std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
};

namespace wininspect {

  struct ServerState
  {
    std::mutex snapshots_mu; // protects snaps, pinned_counts, lru_order, snap_counter, sessions
    std::uint64_t snap_counter = 1;
    std::map<std::string, std::shared_ptr<Snapshot>> snaps;
    std::map<std::string, int> pinned_counts;
    std::list<std::string> lru_order; // LRU: front is oldest, back is newest

    // Client thread tracking (completed threads cleaned by periodic sweep)
    std::list<ThreadHandle> client_threads;
    std::mutex thread_mu; // protects client_threads

    // Event Sequencing
    std::atomic<std::uint64_t> event_counter{1};
    std::vector<Event> event_log;
    size_t max_event_log = 1000;

    // Configurable limits
    size_t max_snapshots = 1000;
    size_t max_sessions = 256;
    size_t max_mem_read_size = 1024 * 1024;      // 1MB default
    size_t max_response_size = 64 * 1024 * 1024; // 64MB default
    int uia_depth = 5;
    int service_timeout_sec = 30;
    int max_connections = 32;
    int session_ttl_sec = 3600; // 1 hour default
    std::atomic<int> active_connections{0};

    // Temporal limits
    int request_timeout_ms = 5000; // 5s watchdog
    int poll_interval_ms = 100;
    int max_wait_ms = 30000;   // 30s max for long polls
    int discovery_port = 1986; // Discovery UDP port
    int rate_limit_ms = 0;
    std::chrono::steady_clock::time_point last_accept_time;

    struct PersistentSession
    {
      std::string last_snap_id;
      bool subscribed = false;
      std::chrono::steady_clock::time_point last_activity;
    };
    std::map<std::string, PersistentSession> sessions;
    // Method authorization sets
    std::set<std::string> allow_methods; // empty = all allowed
    std::set<std::string> deny_methods;  // empty = none denied

    // Network configuration (loaded at startup)
    NetworkConfig net_config;
    // IP access control (CIDR notation)
    std::vector<std::string> allow_cidrs; // empty = all allowed
    std::vector<std::string> deny_cidrs;  // empty = none denied

    // Track evicted snapshot IDs for better error reporting
    std::set<std::string> evicted_snaps;

    // Daemon lifecycle state with explicit phases
    enum class State : uint8_t { Init, Starting, Running, Draining, Stopped, Failed };
    State daemon_state = State::Init;
    std::mutex daemon_state_mu; // protects daemon_state

    /// Convert DaemonState to string for API responses (daemon.status)
    static const char* state_str(State s) {
      switch (s) {
        case State::Init:     return "init";
        case State::Starting: return "starting";
        case State::Running:  return "running";
        case State::Draining: return "draining";
        case State::Stopped:  return "stopped";
        case State::Failed:   return "failed";
        default:              return "unknown";
      }
    }

    // Shared control state machine (accessible from both TCP and named pipe transports)
    std::unique_ptr<wininspectd::ControlManager> control;
  };

  // RAII guard for snapshot pinning. Pins on construction, unpins on destruction.
  // Prevents pinned_counts leaks when exception/break paths skip manual unpin.
  // Move-only — copy would double-unpin.
  class PinGuard
  {
  public:
    PinGuard() = default;
    PinGuard(ServerState* st, const std::string& sid) : st_(st), sid_(sid)
    {
      if (!sid_.empty() && st_) {
        std::lock_guard<std::mutex> lk(st_->snapshots_mu);
        st_->pinned_counts[sid_]++;
      }
    }
    ~PinGuard() { release(); }
    PinGuard(PinGuard&& other) noexcept : st_(other.st_), sid_(std::move(other.sid_))
    {
      other.sid_.clear();
    }
    PinGuard& operator=(PinGuard&& other) noexcept
    {
      if (this != &other) {
        release();
        st_ = other.st_;
        sid_ = std::move(other.sid_);
        other.sid_.clear();
      }
      return *this;
    }
    void reset(ServerState* st, const std::string& sid)
    {
      release();
      st_ = st;
      sid_ = sid;
      if (!sid_.empty() && st_) {
        std::lock_guard<std::mutex> lk(st_->snapshots_mu);
        st_->pinned_counts[sid_]++;
      }
    }
    void clear() { release(); }
    const std::string& sid() const { return sid_; }

  private:
    void release()
    {
      if (!sid_.empty() && st_) {
        std::lock_guard<std::mutex> lk(st_->snapshots_mu);
        st_->pinned_counts[sid_]--;
      }
      sid_.clear();
    }
    ServerState* st_ = nullptr;
    std::string sid_;
    PinGuard(const PinGuard&) = delete;
    PinGuard& operator=(const PinGuard&) = delete;
  };

  struct ClientSession
  {
    SessionID id;
    bool authenticated = false;
    std::string last_snap_id;
    bool subscribed = false;
  };

} // namespace wininspect
