#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

// Shared request-processing logic for pipe and TCP handlers.
// Eliminates ~150 lines of duplicated code between handle_client and
// handle_socket_client. Transport-specific read/write stays in each handler.

#include <future>
#include "server_state.hpp"
#include "wininspect/core.hpp"
#include "wininspect/logger.hpp"

using namespace wininspect;

namespace wininspectd {

// Generate snapshot ID string from counter
inline std::string make_snap_id(std::uint64_t n) {
  return "s-" + std::to_string(n);
}

// Process one protocol request. Returns true if request was handled.
// resp, canonical, and pinned_sid are output parameters.
// Transport-specific read/write and unpin are handled by the caller.
[[nodiscard]] inline bool process_request(
    const std::string &json_req,
    CoreEngine &core, ServerState *st, IBackend *backend,
    ClientSession &session,
    bool read_only, bool no_clipboard, bool require_auth,
    const std::string &auth_keys_data,
    CoreResponse &resp, bool &canonical, std::string &pinned_sid) {
  try {
    auto req = parse_request_json(json_req);
    resp.id = req.id;

    if (!session.authenticated && req.method != "hello") {
      LOG_WARN("Unauthorized request attempted: " + req.method);
      resp.ok = false; resp.error_code = "E_UNAUTHORIZED";
      resp.error_message = "authentication required";
      return true;
    }

    if (!st->allow_methods.empty() && !st->allow_methods.count(req.method)) {
      resp.ok = false; resp.error_code = "E_ACCESS_DENIED";
      resp.error_message = "method not in allow list"; return true;
    }
    if (st->deny_methods.count(req.method)) {
      resp.ok = false; resp.error_code = "E_ACCESS_DENIED";
      resp.error_message = "method is denied"; return true;
    }

    auto itsid = req.params.find("session_id");
    if (itsid != req.params.end() && itsid->second.is_str()) {
      std::string sid_str = itsid->second.as_str();
      std::lock_guard<std::mutex> lk(st->snapshots_mu);
      if (st->sessions.size() >= st->max_sessions && !st->sessions.count(sid_str)) {
        resp.ok = false; resp.error_code = "E_TOO_MANY_SESSIONS";
        resp.error_message = "session limit reached"; return true;
      }
      if (st->sessions.count(sid_str)) {
        auto &ps = st->sessions[sid_str];
        session.id = SessionID(sid_str);
        session.last_snap_id = ps.last_snap_id;
        session.subscribed = ps.subscribed;
        ps.last_activity = std::chrono::steady_clock::now();
      } else {
        session.id = SessionID(sid_str);
        st->sessions[sid_str] = {"", false, std::chrono::steady_clock::now()};
      }
    }

    if (req.method == "events.subscribe") {
      std::string sid;
      {
        auto snap = backend->capture_snapshot();
        std::lock_guard<std::mutex> lk(st->snapshots_mu);
        sid = make_snap_id(st->snap_counter++);
        st->snaps.emplace(sid, std::make_shared<Snapshot>(std::move(snap)));
        st->lru_order.push_back(sid);
        session.subscribed = true; session.last_snap_id = sid;
        if (!session.id.empty()) {
          st->sessions[session.id.val].subscribed = true;
          st->sessions[session.id.val].last_snap_id = sid;
        }
      }
      json::Object o; o["subscribed"] = true; o["snapshot_id"] = sid;
      resp.ok = true; resp.result = o; return true;
    }

    if (req.method == "events.unsubscribe") {
      session.subscribed = false; session.last_snap_id.clear();
      if (!session.id.empty()) {
        std::lock_guard<std::mutex> lk(st->snapshots_mu);
        if (st->sessions.count(session.id.val)) {
          st->sessions[session.id.val].subscribed = false;
          st->sessions[session.id.val].last_snap_id.clear();
        }
      }
      json::Object o; o["unsubscribed"] = true;
      resp.ok = true; resp.result = o; return true;
    }

    if (no_clipboard && (req.method == "clipboard.read" || req.method == "clipboard.write")) {
      resp.ok = false; resp.error_code = "E_ACCESS_DENIED";
      resp.error_message = "clipboard access disabled (--no-clipboard)";
      return true;
    }

    if (req.method == "session.terminate") {
      if (!session.id.empty()) {
        std::lock_guard<std::mutex> lk(st->snapshots_mu);
        st->sessions.erase(session.id.val);
        session.id = SessionID("");
      }
      json::Object o; o["terminated"] = true;
      resp.ok = true; resp.result = o; return true;
    }

    if (read_only && (req.method == "window.postMessage" ||
        req.method == "input.send" || req.method.find("reg.write") != std::string::npos)) {
      resp.ok = false; resp.error_code = "E_ACCESS_DENIED";
      resp.error_message = "daemon is running in read-only mode"; return true;
    }

    auto itc = req.params.find("canonical");
    if (itc != req.params.end() && itc->second.is_bool()) canonical = itc->second.as_bool();

    if (req.method == "snapshot.capture") {
      auto s = backend->capture_snapshot(); std::string sid;
      {
        std::lock_guard<std::mutex> lk(st->snapshots_mu);
        sid = make_snap_id(st->snap_counter++);
        st->snaps.emplace(sid, std::make_shared<Snapshot>(std::move(s)));
        st->lru_order.push_back(sid);
        while (st->lru_order.size() > st->max_snapshots) {
          std::string oldest = st->lru_order.front();
          if (st->pinned_counts[oldest] > 0) {
            st->lru_order.pop_front(); st->lru_order.push_back(oldest); continue;
          }
          st->lru_order.pop_front(); st->snaps.erase(oldest); st->pinned_counts.erase(oldest);
        }
      }
      json::Object o; o["snapshot_id"] = sid;
      resp.ok = true; resp.result = o; return true;
    }

    std::shared_ptr<Snapshot> snap;
    const Snapshot *old_ptr = nullptr;
    Snapshot old_storage;

    auto its = req.params.find("snapshot_id");
    if (its != req.params.end() && its->second.is_str()) {
      std::string sid = its->second.as_str();
      std::lock_guard<std::mutex> lk(st->snapshots_mu);
      auto it = st->snaps.find(sid);
      if (it == st->snaps.end()) {
        resp.ok = false; resp.error_code = "E_BAD_SNAPSHOT";
        resp.error_message = "unknown snapshot_id"; return true;
      }
      snap = it->second; pinned_sid = sid;
      st->pinned_counts[sid]++; st->lru_order.remove(sid); st->lru_order.push_back(sid);
    } else {
      snap = std::make_shared<Snapshot>(backend->capture_snapshot());
    }

    auto itos = req.params.find("old_snapshot_id");
    if (itos != req.params.end() && itos->second.is_str()) {
      std::string osid = itos->second.as_str();
      std::lock_guard<std::mutex> lk(st->snapshots_mu);
      auto it = st->snaps.find(osid);
      if (it != st->snaps.end()) { old_storage = *it->second; old_ptr = &old_storage; }
    } else if (req.method == "events.poll" && !session.last_snap_id.empty()) {
      std::lock_guard<std::mutex> lk(st->snapshots_mu);
      auto it = st->snaps.find(session.last_snap_id);
      if (it != st->snaps.end()) { old_storage = *it->second; old_ptr = &old_storage; }
    }

    auto future = std::async(std::launch::async, [&core, req, snap, old_ptr]() {
      return core.handle(req, *snap, old_ptr);
    });

    if (future.wait_for(std::chrono::milliseconds(st->request_timeout_ms)) ==
        std::future_status::timeout) {
      resp.ok = false; resp.error_code = "E_TIMEOUT";
      resp.error_message = "request timed out";
    } else {
      resp = future.get();
    }

    if (req.method == "events.poll" && resp.ok) {
      auto fresh = backend->capture_snapshot(); std::string sid;
      {
        std::lock_guard<std::mutex> lk(st->snapshots_mu);
        sid = make_snap_id(st->snap_counter++);
        st->snaps.emplace(sid, std::make_shared<Snapshot>(std::move(fresh)));
        st->lru_order.push_back(sid);
        session.last_snap_id = sid;
        if (!session.id.empty()) st->sessions[session.id.val].last_snap_id = sid;
      }
    }
  } catch (const std::exception &e) {
    resp.ok = false; resp.error_code = "E_BAD_REQUEST";
    resp.error_message = e.what();
    LOG_ERROR("Request failed: " + std::string(e.what()));
  }
  return true;
}

} // namespace wininspectd
