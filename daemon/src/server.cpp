#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>

#include "pipe.hpp"

#include "wininspect/core.hpp"
#include "wininspect/win32_backend.hpp"
#include "wininspect/fake_backend.hpp"

#include "tray.hpp"
#include "tcp_server.hpp"

#include <list>

using namespace wininspect;

namespace wininspect {

struct ServerState {
  std::mutex mu;
  std::uint64_t snap_counter = 1;
  std::map<std::string, Snapshot> snaps;
  std::list<std::string> lru_order; // LRU: front is oldest, back is newest
  static constexpr size_t MAX_SNAPSHOTS = 1000; // Increased limit
  bool auth_enabled = false;
};

struct ClientSession {
    std::string last_snap_id;
    bool subscribed = false;
    std::vector<Event> pending_events;
};

}

namespace {

const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\wininspectd";

std::string make_snap_id(std::uint64_t n) {
  return "s-" + std::to_string(n);
}

void handle_client(HANDLE hPipe, ServerState* st, IBackend* backend, bool read_only) {
  CoreEngine core(backend);
  ClientSession session;

  // Each request uses snapshot_id if provided; otherwise uses a new snapshot for pure calls.
  while (true) {
    wininspectd::PipeMessage m;
    if (!wininspectd::pipe_read_message(hPipe, m)) break;

    CoreResponse resp;
    bool canonical = false;

    try {
      auto req = parse_request_json(m.json);
      resp.id = req.id;

      // Security: Check Read-Only mode
      if (read_only && (req.method == "window.postMessage" || req.method == "input.send")) {
          resp.ok = false;
          resp.error_code = "E_ACCESS_DENIED";
          resp.error_message = "daemon is running in read-only mode";
          goto send;
      }

      // canonical flag in params
      auto itc = req.params.find("canonical");
      if (itc != req.params.end() && itc->second.is_bool()) canonical = itc->second.as_bool();

      if (req.method == "snapshot.capture") {
        Snapshot s = backend->capture_snapshot();
        std::string sid;
        {
          std::lock_guard<std::mutex> lk(st->mu);
          sid = make_snap_id(st->snap_counter++);
          st->snaps.emplace(sid, std::move(s));
          st->lru_order.push_back(sid);
          
          if (st->lru_order.size() > ServerState::MAX_SNAPSHOTS) {
            std::string oldest = st->lru_order.front();
            st->lru_order.pop_front();
            st->snaps.erase(oldest);
          }
        }
        json::Object o;
        o["snapshot_id"] = sid;
        resp.ok = true;
        resp.result = o;
      } else if (req.method == "events.subscribe") {
          session.subscribed = true;
          resp.ok = true;
          resp.result = json::Object{};
      } else if (req.method == "events.unsubscribe") {
          session.subscribed = false;
          resp.ok = true;
          resp.result = json::Object{};
      } else if (req.method == "daemon.status") {
        json::Object o;
        {
          std::lock_guard<std::mutex> lk(st->mu);
          o["active_snapshots"] = (double)st->snaps.size();
          o["max_snapshots"] = (double)ServerState::MAX_SNAPSHOTS;
        o["auth_enabled"] = st->auth_enabled;
        }
        o["version"] = "1.0.0";
        resp.ok = true;
        resp.result = o;
      } else {
        // Find snapshot
        Snapshot snap;
        const Snapshot* old_snap_ptr = nullptr;
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
          // LRU Bump
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
            // LRU Bump for old snapshot too
            st->lru_order.remove(osid);
            st->lru_order.push_back(osid);
          }
        } else if (req.method == "events.poll" && !session.last_snap_id.empty()) {
            // Auto-diff against session last state
            std::lock_guard<std::mutex> lk(st->mu);
            auto it = st->snaps.find(session.last_snap_id);
            if (it != st->snaps.end()) {
                old_snap_storage = it->second;
                old_snap_ptr = &old_snap_storage;
            }
        }

        resp = core.handle(req, snap, old_snap_ptr);

        if (req.method == "events.poll" && resp.ok) {
            // In a real system, we'd generate a temporary ID or similar.
            // For now, we capture current state as the 'last known' for next poll.
            Snapshot fresh = backend->capture_snapshot();
            std::string sid;
            {
                std::lock_guard<std::mutex> lk(st->mu);
                sid = make_snap_id(st->snap_counter++);
                st->snaps.emplace(sid, fresh);
                st->lru_order.push_back(sid);
            }
            session.last_snap_id = sid;
        }
      }

    } catch (const std::exception& e) {
      resp.ok = false;
      resp.error_code = "E_BAD_REQUEST";
      resp.error_message = e.what();
      resp.result = json::Null{};
    }

  send:
    auto out = serialize_response_json(resp, canonical);
    if (!wininspectd::pipe_write_message(hPipe, out)) break;
  }

  FlushFileBuffers(hPipe);
  DisconnectNamedPipe(hPipe);
  CloseHandle(hPipe);
}

void run_server(std::atomic<bool>* running, ServerState* st, IBackend* backend, bool read_only) {
  while (running->load()) {
    HANDLE hPipe = CreateNamedPipeW(
      PIPE_NAME,
      PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      PIPE_UNLIMITED_INSTANCES,
      64 * 1024,
      64 * 1024,
      0,
      nullptr
    );

    if (hPipe == INVALID_HANDLE_VALUE) break;

    BOOL ok = ConnectNamedPipe(hPipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (!ok) { CloseHandle(hPipe); continue; }

    std::thread(handle_client, hPipe, st, backend, read_only).detach();
  }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
  bool headless = false;
  bool bind_public = false;
  bool read_only = false;
  std::wstring auth_keys;
  int tcp_port = 1985;
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--headless") headless = true;
    if (std::wstring(argv[i]) == L"--public") bind_public = true;
    if (std::wstring(argv[i]) == L"--read-only") read_only = true;
    if (std::wstring(argv[i]) == L"--auth-keys" && i + 1 < argc) {
      auth_keys = argv[++i];
    }
    if (std::wstring(argv[i]) == L"--port" && i + 1 < argc) {
      tcp_port = std::stoi(argv[++i]);
    }
  }

  // Start TCP server for cross-environment access (Host <-> Guest, Host <-> Wine)
  std::string auth_keys_u8;
  if (!auth_keys.empty()) {
      int len = WideCharToMultiByte(CP_UTF8, 0, auth_keys.c_str(), (int)auth_keys.size(), nullptr, 0, nullptr, nullptr);
      auth_keys_u8.resize(len);
      WideCharToMultiByte(CP_UTF8, 0, auth_keys.c_str(), (int)auth_keys.size(), auth_keys_u8.data(), len, nullptr, nullptr);
  }

  ServerState st;
  st.auth_enabled = !auth_keys_u8.empty();
  Win32Backend backend;
  std::atomic<bool> running{true};

  std::thread server_thread(run_server, &running, &st, &backend, read_only);

  std::thread([&, tcp_port, bind_public, auth_keys_u8, read_only]() {
    wininspectd::TcpServer tcp(tcp_port, &st, &backend);
    tcp.start(&running, bind_public, auth_keys_u8, read_only);
  }).detach();

  if (!headless) {
    wininspectd::TrayManager tray([&]() {
      running = false;
      // We use exit(0) to ensure the process terminates even if server_thread is blocked on ConnectNamedPipe
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
