#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

// Shared request-processing logic for pipe, TCP, and HTTP handlers.
// Eliminates duplicated code between transports. Each transport handles
// only framing/encryption; all protocol logic lives here.

#include "server_state.hpp"
#include "wininspect/core.hpp"
#include "wininspect/compress.hpp"
#include "wininspect/logger.hpp"

using namespace wininspect;

namespace wininspectd {

  // Generate snapshot ID string from counter
  inline std::string make_snap_id(std::uint64_t n)
  {
    return "s-" + std::to_string(n);
  }

  // ── Helper: parameter extraction ────────────────────────────────────────────
  static std::optional<std::string> rh_get_str(const json::Object& o, const std::string& k)
  {
    auto it = o.find(k);
    if (it != o.end() && it->second.is_str())
      return it->second.as_str();
    return std::nullopt;
  }
  static std::optional<double> rh_get_num(const json::Object& o, const std::string& k)
  {
    auto it = o.find(k);
    if (it != o.end() && it->second.is_num())
      return it->second.as_num();
    return std::nullopt;
  }

  // ── Response preparation ───────────────────────────────────────────────────
  // Prepares the wire-format response: applies size cap, optionally compresses.
  // Output: raw_out (wire bytes), was_compressed (true if compressed framing used).
  // Returns false if the connection should close (write error).
  inline bool prepare_response(const CoreResponse& resp, bool canonical, size_t max_response_size,
                               bool encrypted, bool compress_responses, std::string& raw_out,
                               bool& was_compressed)
  {
    raw_out = serialize_response_json(resp, canonical);
    was_compressed = false;

    // Enforce response size cap
    if (raw_out.size() > max_response_size) {
      CoreResponse capped = resp;
      capped.ok = false;
      capped.error_code = "E_RESPONSE_TOO_LARGE";
      capped.error_message = "response exceeds limit";
      raw_out = serialize_response_json(capped, canonical);
    }

    // Compress large plaintext responses (threshold: 1KB)
    // Encrypted path uses its own framing; compression skipped for encrypted.
    if (!encrypted && compress_responses && raw_out.size() > 1024) {
      std::vector<uint8_t> raw(raw_out.begin(), raw_out.end());
      auto compressed = wininspect::compress(raw);
      if (!compressed.empty() && compressed.size() < raw.size()) {
        // Compressed framing: [4B raw_size + compressed_data], with FRAME_COMPRESSED_FLAG
        uint32_t clen = ((uint32_t)compressed.size() + 4) | wininspect::FRAME_COMPRESSED_FLAG;
        raw_out.assign((const char*)&clen, 4);
        uint32_t raw_size_net = htonl((uint32_t)raw.size());
        raw_out.append((const char*)&raw_size_net, 4);
        raw_out.append((const char*)compressed.data(), compressed.size());
        was_compressed = true;
      }
    }
    return true;
  }

  // ── Main request processor ─────────────────────────────────────────────────
  // Processes one protocol request. Handles session, snapshot, events, control,
  // auth, capabilities, and dispatch. Transport-specific framing is handled by
  // the caller.
  //
  // Parameters:
  //   json_req          — raw JSON request string
  //   core              — CoreEngine for method dispatch
  //   st                — shared ServerState
  //   backend           — IBackend for system operations
  //   session           — per-connection session state (in/out)
  //   read_only         — if true, block mutating methods
  //   no_clipboard      — if true, block clipboard methods
  //   require_auth      — if true, require authentication
  //   auth_keys_data    — authorized public keys
  //   version_checked   — per-connection flag, set true after first version check
  //   compress_responses — per-connection flag, set true if client accepts zlib
  //   resp              — output: the CoreResponse
  //   canonical         — output: whether client requested canonical format
  //   pin_guard         — output: manages snapshot pin lifetime
  //   close_connection  — output: if true, caller should close connection
  //
  // Returns false only if json_req is malformed (parse error).
  inline bool process_request(const std::string& json_req, CoreEngine& core, ServerState* st,
                              IBackend* backend, ClientSession& session, bool read_only,
                              bool no_clipboard, bool require_auth,
                              const std::string& auth_keys_data, bool& version_checked,
                              bool& compress_responses, CoreResponse& resp, bool& canonical,
                              PinGuard& pin_guard, bool& close_connection)
  {

    close_connection = false;
    resp.ok = true;
    resp.result = json::Null{};

    try {
      auto req = parse_request_json(json_req);
      resp.id = req.id;

      // ── Compression negotiation ──────────────────────────────────────────
      auto it_enc = req.params.find("accept_encoding");
      if (it_enc != req.params.end() && it_enc->second.is_str() &&
          it_enc->second.as_str().find("zlib") != std::string::npos) {
        compress_responses = true;
      }

      // ── Protocol version check (once per connection) ─────────────────────
      if (!version_checked) {
        version_checked = true;
        auto it_ver = req.params.find("protocol_version");
        if (it_ver != req.params.end() && it_ver->second.is_str()) {
          if (it_ver->second.as_str() != PROTOCOL_VERSION) {
            resp.ok = false;
            resp.error_code = "E_PROTOCOL_VERSION";
            resp.error_message = std::string("expected ") + std::string(PROTOCOL_VERSION) +
                                 ", got " + it_ver->second.as_str();
            return true;
          }
        }
        else {
          LOG_DEBUG("Client did not send protocol_version — accepting legacy");
        }
      }

      // ── Auth check (pipe: already authed in handle_client) ───────────────
      if (!session.authenticated && req.method != "hello") {
        LOG_WARN("Unauthorized request attempted: " + req.method);
        resp.ok = false;
        resp.error_code = "E_UNAUTHORIZED";
        resp.error_message = "authentication required";
        return true;
      }

      // ── Session management ───────────────────────────────────────────────
      auto itsid = req.params.find("session_id");
      if (itsid != req.params.end() && itsid->second.is_str()) {
        std::string sid_str = itsid->second.as_str();
        std::lock_guard<std::mutex> lk(st->snapshots_mu);
        if (st->sessions.size() >= st->max_sessions && !st->sessions.count(sid_str)) {
          resp.ok = false;
          resp.error_code = "E_TOO_MANY_SESSIONS";
          resp.error_message = "session limit reached";
          return true;
        }
        if (st->sessions.count(sid_str)) {
          auto& ps = st->sessions[sid_str];
          session.id = SessionID(sid_str);
          session.last_snap_id = ps.last_snap_id;
          session.subscribed = ps.subscribed;
          ps.last_activity = std::chrono::steady_clock::now();
        }
        else {
          session.id = SessionID(sid_str);
          st->sessions[sid_str] = {"", false, std::chrono::steady_clock::now()};
        }
      }

      // ── events.subscribe ─────────────────────────────────────────────────
      if (req.method == "events.subscribe") {
        std::string sid;
        {
          auto snap = backend->capture_snapshot();
          std::lock_guard<std::mutex> lk(st->snapshots_mu);
          sid = make_snap_id(st->snap_counter++);
          st->snaps.emplace(sid, std::make_shared<Snapshot>(std::move(snap)));
          st->lru_order.push_back(sid);
          session.subscribed = true;
          session.last_snap_id = sid;
          if (!session.id.empty()) {
            st->sessions[session.id.val].subscribed = true;
            st->sessions[session.id.val].last_snap_id = sid;
          }
        }
        json::Object o;
        o["subscribed"] = true;
        o["snapshot_id"] = sid;
        resp.ok = true;
        resp.result = o;
        return true;
      }

      // ── events.unsubscribe ───────────────────────────────────────────────
      if (req.method == "events.unsubscribe") {
        session.subscribed = false;
        session.last_snap_id.clear();
        if (!session.id.empty()) {
          std::lock_guard<std::mutex> lk(st->snapshots_mu);
          if (st->sessions.count(session.id.val)) {
            st->sessions[session.id.val].subscribed = false;
            st->sessions[session.id.val].last_snap_id.clear();
          }
        }
        json::Object o;
        o["unsubscribed"] = true;
        resp.ok = true;
        resp.result = o;
        return true;
      }

      // ── Control methods (shared via ServerState) ─────────────────────────
      if (st->control) {
        if (req.method == "control.take") {
          auto who_s = rh_get_str(req.params, "controller").value_or("human");
          auto who = wininspect::controller_type_from_str(who_s);
          auto id = rh_get_str(req.params, "id").value_or("");
          auto ok = st->control->take_control(who, id);
          if (!ok) {
            resp.ok = false;
            resp.error_code = "E_CONTROL_DENIED";
            resp.error_message = "cannot take control";
            return true;
          }
          json::Object o;
          o["controller"] = who_s;
          o["ok"] = ok;
          resp.ok = true;
          resp.result = o;
          return true;
        }
        if (req.method == "control.release") {
          auto who_s = rh_get_str(req.params, "controller").value_or("human");
          auto who = wininspect::controller_type_from_str(who_s);
          auto id = rh_get_str(req.params, "id").value_or("");
          (void)st->control->release_control(who, id);
          json::Object o;
          o["ok"] = true;
          resp.ok = true;
          resp.result = o;
          return true;
        }
        if (req.method == "control.status") {
          resp.ok = true;
          resp.result = st->control->get_status();
          return true;
        }
        if (req.method == "control.setMode") {
          auto mode = rh_get_str(req.params, "mode").value_or("hybrid");
          st->control->set_operation_mode(mode);
          json::Object o;
          o["mode"] = mode;
          o["ok"] = true;
          resp.ok = true;
          resp.result = o;
          return true;
        }
        if (req.method == "control.auditLog") {
          auto max = (size_t)rh_get_num(req.params, "max").value_or(100);
          resp.ok = true;
          resp.result = st->control->get_audit_log(max);
          return true;
        }
      }

      // ── Clipboard gate ──────────────────────────────────────────────────
      if (no_clipboard && (req.method == "clipboard.read" || req.method == "clipboard.write")) {
        resp.ok = false;
        resp.error_code = "E_ACCESS_DENIED";
        resp.error_message = "clipboard access disabled (--no-clipboard)";
        return true;
      }

      // ── Method allow/deny ────────────────────────────────────────────────
      if (!st->allow_methods.empty() && !st->allow_methods.count(req.method)) {
        resp.ok = false;
        resp.error_code = "E_ACCESS_DENIED";
        resp.error_message = "method not in allow list";
        return true;
      }
      if (st->deny_methods.count(req.method)) {
        resp.ok = false;
        resp.error_code = "E_ACCESS_DENIED";
        resp.error_message = "method is denied";
        return true;
      }

      // ── session.terminate ────────────────────────────────────────────────
      if (req.method == "session.terminate") {
        if (!session.id.empty()) {
          std::lock_guard<std::mutex> lk(st->snapshots_mu);
          st->sessions.erase(session.id.val);
          session.id = SessionID("");
        }
        json::Object o;
        o["terminated"] = true;
        resp.ok = true;
        resp.result = o;
        return true;
      }

      // ── canonical format ────────────────────────────────────────────────
      auto itc = req.params.find("canonical");
      if (itc != req.params.end() && itc->second.is_bool())
        canonical = itc->second.as_bool();

      // ── snapshot.capture ─────────────────────────────────────────────────
      if (req.method == "snapshot.capture") {
        auto s = backend->capture_snapshot();
        std::string sid;
        {
          std::lock_guard<std::mutex> lk(st->snapshots_mu);
          sid = make_snap_id(st->snap_counter++);
          st->snaps.emplace(sid, std::make_shared<Snapshot>(std::move(s)));
          st->lru_order.push_back(sid);
          while (st->lru_order.size() > st->max_snapshots) {
            std::string oldest = st->lru_order.front();
            if (st->pinned_counts[oldest] > 0) {
              st->lru_order.pop_front();
              st->lru_order.push_back(oldest);
              continue;
            }
            st->lru_order.pop_front();
            st->snaps.erase(oldest);
            st->pinned_counts.erase(oldest);
            st->evicted_snaps.insert(oldest);
          }
        }
        json::Object o;
        o["snapshot_id"] = sid;
        resp.ok = true;
        resp.result = o;
        return true;
      }

      // ── Snapshot lookup for method dispatch ──────────────────────────────
      std::shared_ptr<Snapshot> snap;
      const Snapshot* old_ptr = nullptr;
      Snapshot old_storage;

      auto its = req.params.find("snapshot_id");
      if (its != req.params.end() && its->second.is_str()) {
        std::string sid = its->second.as_str();
        std::lock_guard<std::mutex> lk(st->snapshots_mu);
        auto it = st->snaps.find(sid);
        if (it == st->snaps.end()) {
          if (st->evicted_snaps.count(sid)) {
            resp.ok = false;
            resp.error_code = E_SNAPSHOT_EVICTED;
            resp.error_message = "snapshot was evicted";
          }
          else {
            resp.ok = false;
            resp.error_code = "E_BAD_SNAPSHOT";
            resp.error_message = "unknown snapshot_id";
          }
          return true;
        }
        snap = it->second;
        pin_guard.reset(st, sid);
        st->lru_order.remove(sid);
        st->lru_order.push_back(sid);
      }
      else {
        snap = std::make_shared<Snapshot>(backend->capture_snapshot());
      }

      // Old snapshot for diff-based methods
      auto itos = req.params.find("old_snapshot_id");
      if (itos != req.params.end() && itos->second.is_str()) {
        std::string osid = itos->second.as_str();
        std::lock_guard<std::mutex> lk(st->snapshots_mu);
        auto it = st->snaps.find(osid);
        if (it != st->snaps.end()) {
          old_storage = *it->second;
          old_ptr = &old_storage;
        }
      }
      else if (req.method == "events.poll" && !session.last_snap_id.empty()) {
        std::lock_guard<std::mutex> lk(st->snapshots_mu);
        auto it = st->snaps.find(session.last_snap_id);
        if (it != st->snaps.end()) {
          old_storage = *it->second;
          old_ptr = &old_storage;
        }
      }

      // ── Dispatch ─────────────────────────────────────────────────────────
      // Direct call (no std::async). Socket SO_RCVTIMEO provides safety:
      // if core.handle() hangs, the connection will eventually time out.
      // std::async adds ~2s thread creation overhead under Wine 9.x.
      resp = core.handle(req, *snap, old_ptr);

      // ── events.poll post-handle snapshot ─────────────────────────────────
      if (req.method == "events.poll" && resp.ok) {
        auto fresh = backend->capture_snapshot();
        std::string sid;
        {
          std::lock_guard<std::mutex> lk(st->snapshots_mu);
          sid = make_snap_id(st->snap_counter++);
          st->snaps.emplace(sid, std::make_shared<Snapshot>(std::move(fresh)));
          st->lru_order.push_back(sid);
          session.last_snap_id = sid;
          if (!session.id.empty())
            st->sessions[session.id.val].last_snap_id = sid;
        }
      }
    }
    catch (const std::exception& e) {
      resp.ok = false;
      resp.error_code = "E_BAD_REQUEST";
      resp.error_message = e.what();
      LOG_ERROR("Request failed: " + std::string(e.what()));
    }
    catch (...) {
      resp.ok = false;
      resp.error_code = "E_BAD_REQUEST";
      resp.error_message = "unexpected error";
    }
    return true;
  }

} // namespace wininspectd
