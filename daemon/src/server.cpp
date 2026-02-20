#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <windows.h>

#include "pipe.hpp"

#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"
#include "wininspect/win32_backend.hpp"
#include "wininspect/util_win32.hpp"

#include "tcp_server.hpp"
#include "tray.hpp"

#include <list>

using namespace wininspect;

namespace wininspect {

struct ServerState {
  std::mutex mu;
  std::uint64_t snap_counter = 1;
  std::map<std::string, Snapshot> snaps;
  std::map<std::string, int> pinned_counts;
  std::list<std::string> lru_order; // LRU: front is oldest, back is newest
  static constexpr size_t MAX_SNAPSHOTS = 1000;
  std::atomic<int> active_connections{0};
  static constexpr int MAX_CONNECTIONS = 32;

  struct PersistentSession {
    std::string last_snap_id;
    bool subscribed = false;
    std::chrono::steady_clock::time_point last_activity;
  };
  std::map<std::string, PersistentSession> sessions;
};

struct ClientSession {
  std::string id;
  bool authenticated = false;
  std::string last_snap_id;
  bool subscribed = false;
};

} // namespace wininspect

namespace {

const wchar_t *PIPE_NAME = L"\\\\.\\pipe\\wininspectd";

std::string make_snap_id(std::uint64_t n) { return "s-" + std::to_string(n); }

void handle_client(HANDLE hPipe, ServerState *st, IBackend *backend,
                   bool read_only, const std::string& auth_keys_u8) {
  CoInitGuard coinit;
  CoreEngine core(backend);
  ClientSession session;
  st->active_connections++;

  // Handle auto-auth for local pipes if no keys set
  if (auth_keys_u8.empty()) session.authenticated = true;

  // Ensure decrement on exit
  struct ConnGuard {
    std::atomic<int>& count;
    ~ConnGuard() { count--; }
  } guard{st->active_connections};

  while (true) {
    wininspectd::PipeMessage m;
    if (!wininspectd::pipe_read_message(hPipe, m))
      break;

    CoreResponse resp;
    bool canonical = false;
    std::string pinned_sid;

    try {
      auto req = parse_request_json(m.json);
      resp.id = req.id;

      // 1. Handshake Enforcement
      if (!session.authenticated && req.method != "hello") {
        resp.ok = false;
        resp.error_code = "E_UNAUTHORIZED";
        resp.error_message = "authentication required";
        goto send;
      }

      // session_id in params for persistence
      auto itsid = req.params.find("session_id");
      if (itsid != req.params.end() && itsid->second.is_str()) {
        std::string sid = itsid->second.as_str();
        std::lock_guard<std::mutex> lk(st->mu);
        if (st->sessions.count(sid)) {
          auto &ps = st->sessions[sid];
          session.id = sid;
          session.last_snap_id = ps.last_snap_id;
          session.subscribed = ps.subscribed;
          ps.last_activity = std::chrono::steady_clock::now();
        } else {
          // New session
          session.id = sid;
          st->sessions[sid] = { "", false, std::chrono::steady_clock::now() };
        }
      }

      // Security: Check Read-Only mode
      if (read_only &&
          (req.method == "window.postMessage" || req.method == "input.send" || req.method.find("reg.write") != std::string::npos)) {
        resp.ok = false;
        resp.error_code = "E_ACCESS_DENIED";
        resp.error_message = "daemon is running in read-only mode";
        goto send;
      }

      auto itc = req.params.find("canonical");
      if (itc != req.params.end() && itc->second.is_bool())
        canonical = itc->second.as_bool();

      if (req.method == "snapshot.capture") {
        Snapshot s = backend->capture_snapshot();
        std::string sid;
        {
          std::lock_guard<std::mutex> lk(st->mu);
          sid = make_snap_id(st->snap_counter++);
          st->snaps.emplace(sid, std::move(s));
          st->lru_order.push_back(sid);

          while (st->lru_order.size() > ServerState::MAX_SNAPSHOTS) {
            std::string oldest = st->lru_order.front();
            // Check pinning
            if (st->pinned_counts[oldest] > 0) {
               // Move to back of LRU to give it more time, don't evict yet
               st->lru_order.pop_front();
               st->lru_order.push_back(oldest);
               continue; 
            }
            st->lru_order.pop_front();
            st->snaps.erase(oldest);
            st->pinned_counts.erase(oldest);
          }
        }
        json::Object o;
        o["snapshot_id"] = sid;
        resp.ok = true;
        resp.result = o;
      } else {
        Snapshot snap;
        const Snapshot *old_snap_ptr = nullptr;
        Snapshot old_snap_storage;

        auto its = req.params.find("snapshot_id");
        if (its != req.params.end() && its->second.is_str()) {
          std::string sid = its->second.as_str();
          std::lock_guard<std::mutex> lk(st->mu);
          auto it = st->snaps.find(sid);
          if (it == st->snaps.end()) {
            resp.ok = false;
            resp.error_code = "E_BAD_SNAPSHOT";
            resp.error_message = "unknown or evicted snapshot_id";
            goto send;
          }
          snap = it->second;
          pinned_sid = sid;
          st->pinned_counts[sid]++;
          st->lru_order.remove(sid);
          st->lru_order.push_back(sid);
        } else {
          snap = backend->capture_snapshot();
        }

        auto itos = req.params.find("old_snapshot_id");
        if (itos != req.params.end() && itos->second.is_str()) {
          std::string osid = itos->second.as_str();
          std::lock_guard<std::mutex> lk(st->mu);
          auto it = st->snaps.find(osid);
          if (it != st->snaps.end()) {
            old_snap_storage = it->second;
            old_snap_ptr = &old_snap_storage;
          }
        } else if (req.method == "events.poll" && !session.last_snap_id.empty()) {
          std::lock_guard<std::mutex> lk(st->mu);
          auto it = st->snaps.find(session.last_snap_id);
          if (it != st->snaps.end()) {
            old_snap_storage = it->second;
            old_snap_ptr = &old_snap_storage;
          }
        }

        resp = core.handle(req, snap, old_snap_ptr);

        if (req.method == "events.poll" && resp.ok) {
          Snapshot fresh = backend->capture_snapshot();
          std::string sid;
          {
            std::lock_guard<std::mutex> lk(st->mu);
            sid = make_snap_id(st->snap_counter++);
            st->snaps.emplace(sid, fresh);
            st->lru_order.push_back(sid);
            session.last_snap_id = sid;
            if (!session.id.empty()) {
              st->sessions[session.id].last_snap_id = sid;
            }
          }
        }
      }

    } catch (const std::exception &e) {
      resp.ok = false;
      resp.error_code = "E_BAD_REQUEST";
      resp.error_message = e.what();
      resp.result = json::Null{};
    }

  send:
    auto out = serialize_response_json(resp, canonical);
    wininspectd::pipe_write_message(hPipe, out);
    
    // Unpin
    if (!pinned_sid.empty()) {
      std::lock_guard<std::mutex> lk(st->mu);
      st->pinned_counts[pinned_sid]--;
    }
  }

  FlushFileBuffers(hPipe);
  DisconnectNamedPipe(hPipe);
  CloseHandle(hPipe);
}

void run_server(std::atomic<bool> *running, ServerState *st,
                IBackend *backend, bool read_only, std::string auth_keys_u8) {
  while (running->load()) {
    HANDLE hPipe = CreateNamedPipeW(
        PIPE_NAME, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, nullptr);

    if (hPipe == INVALID_HANDLE_VALUE)
      break;

    BOOL ok = ConnectNamedPipe(hPipe, nullptr)
                  ? TRUE
                  : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (!ok) {
      CloseHandle(hPipe);
      continue;
    }

    if (st->active_connections >= ServerState::MAX_CONNECTIONS) {
      // Too many connections, drop this one
      DisconnectNamedPipe(hPipe);
      CloseHandle(hPipe);
      continue;
    }

    std::thread(handle_client, hPipe, st, backend, read_only, auth_keys_u8).detach();
  }
}

} // namespace

int main(int argc, char **argv) {
  bool headless = false;
  bool bind_public = false;
  bool read_only = false;
  std::string auth_keys;
  int tcp_port = 1985;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--headless")
      headless = true;
    if (std::string(argv[i]) == "--public")
      bind_public = true;
    if (std::string(argv[i]) == "--read-only")
      read_only = true;
    if (std::string(argv[i]) == "--auth-keys" && i + 1 < argc) {
      auth_keys = argv[++i];
    }
    if (std::string(argv[i]) == "--port" && i + 1 < argc) {
      tcp_port = std::stoi(argv[++i]);
    }
  }

  ServerState st;
  Win32Backend backend;
  std::atomic<bool> running{true};

  std::string auth_keys_u8 = auth_keys;

  std::thread server_thread(run_server, &running, &st, &backend, read_only, auth_keys_u8);

  // Start TCP server for cross-environment access (Host <-> Guest, Host <->
  // Wine)
  std::thread([&, tcp_port, bind_public, auth_keys_u8, read_only]() {
    wininspectd::TcpServer tcp(tcp_port, &st, &backend);
    tcp.start(&running, bind_public, auth_keys_u8, read_only);
  }).detach();

  if (!headless) {
    wininspectd::TrayManager tray([&]() {
      running = false;
      // We use exit(0) to ensure the process terminates even if server_thread
      // is blocked on ConnectNamedPipe
      exit(0);
    });

    if (tray.init(GetModuleHandle(nullptr))) {
      tray.run();
    }
  }

  if (server_thread.joinable()) {
    if (headless) {
      server_thread.join();
    } else {
      server_thread.detach(); // if not headless, we likely exited via tray
    }
  }

  return 0;
}
#endif
