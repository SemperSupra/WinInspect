// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#ifdef _WIN32
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "pipe.hpp"
#include "server_state.hpp"

#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"
#include "wininspect/win32_backend.hpp"
#include "wininspect/util_win32.hpp"

#include "tcp_server.hpp"
#include "tray.hpp"

#include <list>
#include <future>
#include <memory>
#include <sstream>
#include <fstream>

using namespace wininspect;

namespace {

std::wstring g_pipe_name = L"\\\\.\\pipe\\wininspectd";

std::string make_snap_id(std::uint64_t n) { return "s-" + std::to_string(n); }

void cleanup_sessions(ServerState *st) {
  std::lock_guard<std::mutex> lk(st->mu);
  auto now = std::chrono::steady_clock::now();
  for (auto it = st->sessions.begin(); it != st->sessions.end(); ) {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_activity).count();
    if (elapsed > st->session_ttl_sec) {
      it = st->sessions.erase(it);
    } else {
      ++it;
    }
  }
}

void handle_client(HANDLE hPipe, ServerState *st, IBackend *backend,
                   bool read_only, bool require_auth, bool admin_logs,
                   const std::string& auth_keys_data) {
  CoInitGuard coinit;
  CoreEngine core(backend);
  core.set_admin_logs_enabled(admin_logs);
  ClientSession session;
  st->active_connections++;
  LOG_INFO("New client connection established.");

  // Auto-auth local pipes only when not in require-auth mode and no keys configured
  if (!require_auth && auth_keys_data.empty()) {
    session.authenticated = true;
    LOG_DEBUG("Local auto-auth enabled (no keys, not require-auth).");
  }

  // Ensure decrement on exit
  struct ConnGuard {
    std::atomic<int>& count;
    ~ConnGuard() { 
      count--; 
      LOG_INFO("Client connection closed.");
    }
  } guard{st->active_connections};

  std::string pinned_sid;
  while (true) {
    wininspectd::PipeMessage m;
    if (!wininspectd::pipe_read_message(hPipe, m))
      break;

    CoreResponse resp;
    bool canonical = false;
    pinned_sid.clear();

    try {
      auto req = parse_request_json(m.json);
      resp.id = req.id;

      // 1. Handshake Enforcement
      if (!session.authenticated && req.method != "hello") {
        LOG_WARN("Unauthorized request attempted: " + req.method);
        resp.ok = false;
        resp.error_code = "E_UNAUTHORIZED";
        resp.error_message = "authentication required";
        goto send;
      }

      auto itsid = req.params.find("session_id");
      if (itsid != req.params.end() && itsid->second.is_str()) {
        std::string sid_str = itsid->second.as_str();
        std::lock_guard<std::mutex> lk(st->mu);
        if (st->sessions.count(sid_str)) {
          auto &ps = st->sessions[sid_str];
          session.id = SessionID(sid_str);
          session.last_snap_id = ps.last_snap_id;
          session.subscribed = ps.subscribed;
          ps.last_activity = std::chrono::steady_clock::now();
          LOG_DEBUG("Recovered session: " + sid_str);
        } else {
          // New session
          session.id = SessionID(sid_str);
          st->sessions[sid_str] = { "", false, std::chrono::steady_clock::now() };
          LOG_DEBUG("Created persistent session: " + sid_str);
        }
      }


      // Intercept subscribe before core dispatch -- captures baseline snapshot
      if (req.method == "events.subscribe") {
        Snapshot s = backend->capture_snapshot();
        std::string sid;
        {
          std::lock_guard<std::mutex> lk(st->mu);
          sid = make_snap_id(st->snap_counter++);
          st->snaps.emplace(sid, std::move(s));
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
        goto send;
      }

      if (req.method == "events.unsubscribe") {
        session.subscribed = false;
        session.last_snap_id.clear();
        if (!session.id.empty()) {
          std::lock_guard<std::mutex> lk(st->mu);
          if (st->sessions.count(session.id.val)) {
            st->sessions[session.id.val].subscribed = false;
            st->sessions[session.id.val].last_snap_id.clear();
          }
        }
        json::Object o;
        o["unsubscribed"] = true;
        resp.ok = true;
        resp.result = o;
        goto send;
      }
      if (req.method == "session.terminate") {
        if (!session.id.empty()) {
          std::lock_guard<std::mutex> lk(st->mu);
          st->sessions.erase(session.id.val);
          LOG_INFO("Session terminated explicitly: " + session.id.val);
          session.id = SessionID(""); // Clear local ref
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

          while (st->lru_order.size() > st->max_snapshots) {
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

        // Watchdog: run core in async task
        auto future = std::async(std::launch::async, [&]() {
          return core.handle(req, snap, old_snap_ptr);
        });

        if (future.wait_for(std::chrono::milliseconds(st->request_timeout_ms)) == std::future_status::timeout) {
          resp.ok = false;
          resp.error_code = "E_TIMEOUT";
          resp.error_message = "request timed out in core engine";
        } else {
          resp = future.get();
        }

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
              st->sessions[session.id.val].last_snap_id = sid;
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

  // Unpin any snapshot still pinned from an uncompleted request
  if (!pinned_sid.empty()) {
    std::lock_guard<std::mutex> lk(st->mu);
    st->pinned_counts[pinned_sid]--;
  }

  FlushFileBuffers(hPipe);
  DisconnectNamedPipe(hPipe);
  CloseHandle(hPipe);
}

void run_server(std::atomic<bool> *running, ServerState *st,
                IBackend *backend, bool read_only, bool require_auth,
                bool admin_logs, std::string auth_keys_data) {
  std::string pipe_name_narrow(g_pipe_name.begin(), g_pipe_name.end());
  LOG_INFO("Named Pipe server starting on: " + pipe_name_narrow);
  while (running->load()) {
    HANDLE hPipe = CreateNamedPipeW(
        g_pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, nullptr);

    if (hPipe == INVALID_HANDLE_VALUE) {
      LOG_ERROR("Failed to create Named Pipe: " + std::to_string(GetLastError()));
      break;
    }

    BOOL ok = ConnectNamedPipe(hPipe, nullptr)
                  ? TRUE
                  : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (!ok) {
      CloseHandle(hPipe);
      continue;
    }

    LOG_DEBUG("Named Pipe connection accepted.");

    if (st->active_connections >= st->max_connections) {
      // Too many connections, drop this one
      DisconnectNamedPipe(hPipe);
      CloseHandle(hPipe);
      continue;
    }

    {
    std::lock_guard<std::mutex> lk(st->thread_mu);
    st->client_threads.emplace_back(handle_client, hPipe, st, backend, read_only, require_auth, admin_logs, auth_keys_data);
  }
  }
}

void run_discovery_responder(std::atomic<bool> *running, ServerState *st, int tcp_port, IBackend *backend, bool include_hostname) {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) return;

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((unsigned short)st->discovery_port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    closesocket(s);
    return;
  }

  LOG_INFO("Discovery responder listening on UDP " + std::to_string(st->discovery_port));

  while (running->load()) {
    char buf[512];
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    int r = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&client_addr, &client_addr_len);
    if (r > 0) {
      std::string msg(buf, r);
      LOG_DEBUG("Discovery packet received: " + msg);
      if (msg == "WININSPECT_DISCOVER") {
        auto env = backend->get_env_metadata();
        json::Object resp;
        resp["type"] = "announcement";
        resp["port"] = (double)tcp_port;
        resp["os"] = env.at("os").as_str();
        resp["is_wine"] = env.at("is_wine").as_bool();
        
        // Extract short pipe name from full path for discovery
        std::string full_pipe(g_pipe_name.begin(), g_pipe_name.end());
        size_t last_bs = full_pipe.rfind('\\');
        resp["pipe_name"] = (last_bs != std::string::npos) ? full_pipe.substr(last_bs + 1) : full_pipe;

        if (include_hostname) {
          char hostname_buf[256];
          gethostname(hostname_buf, sizeof(hostname_buf));
          resp["hostname"] = std::string(hostname_buf);
        }

        std::string out = json::dumps(resp);
        sendto(s, out.data(), (int)out.size(), 0, (struct sockaddr *)&client_addr, client_addr_len);
      }
    }
  }
  closesocket(s);
}

} // namespace

int main(int argc, char **argv) {
  bool headless = false;
  bool bind_public = false;
  bool read_only = false;
  bool include_hostname = false;
  bool admin_logs = false;
  std::string auth_keys;
  bool require_auth = false;
  int tcp_port = 1985;
  int max_snaps = 1000;
  int max_conns = 32;
  int session_ttl = 3600;
  int req_timeout = 5000;
  int poll_interval = 100;
  int max_wait = 30000;
  int discovery_port = 1986;
  int max_mem_read = 1024 * 1024;
  int uia_depth = -1; // -1 means use backend default
  int service_timeout = 30;
  int max_event_log = 1000;

  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--headless")
      headless = true;
    if (std::string(argv[i]) == "--public")
      bind_public = true;
    if (std::string(argv[i]) == "--read-only")
      read_only = true;
    if (std::string(argv[i]) == "--require-auth")
      require_auth = true;
    if (std::string(argv[i]) == "--include-hostname")
      include_hostname = true;
    if (std::string(argv[i]) == "--admin-logs")
      admin_logs = true;
    if (std::string(argv[i]) == "--auth-keys" && i + 1 < argc) {
      auth_keys = argv[++i];
    }
    if (std::string(argv[i]) == "--port" && i + 1 < argc) {
      tcp_port = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--max-snapshots" && i + 1 < argc) {
      max_snaps = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--max-conns" && i + 1 < argc) {
      max_conns = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--session-ttl" && i + 1 < argc) {
      session_ttl = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--request-timeout" && i + 1 < argc) {
      req_timeout = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--poll-interval" && i + 1 < argc) {
      poll_interval = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--max-wait" && i + 1 < argc) {
      max_wait = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--pipe-name" && i + 1 < argc) {
      std::string name = argv[++i];
      std::wstring wname(name.begin(), name.end());
      g_pipe_name = L"\\\\.\\pipe\\" + wname;
    }
    if (std::string(argv[i]) == "--discovery-port" && i + 1 < argc) {
      discovery_port = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--max-mem-read" && i + 1 < argc) {
      max_mem_read = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--uia-depth" && i + 1 < argc) {
      uia_depth = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--service-timeout" && i + 1 < argc) {
      service_timeout = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--max-event-log" && i + 1 < argc) {
      max_event_log = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--log-level" && i + 1 < argc) {
      std::string lvl = argv[++i];
      if (lvl == "TRACE") Logger::get().set_level(LogLevel::TRACE);
      else if (lvl == "DEBUG") Logger::get().set_level(LogLevel::DEBUG);
      else if (lvl == "INFO") Logger::get().set_level(LogLevel::INFO);
      else if (lvl == "WARN") Logger::get().set_level(LogLevel::WARN);
      else if (lvl == "ERROR") Logger::get().set_level(LogLevel::ERR);
    }
  }

  auto st = std::make_unique<ServerState>();
  st->max_snapshots = (size_t)max_snaps;
  st->max_connections = max_conns;
  st->session_ttl_sec = session_ttl;
  st->request_timeout_ms = req_timeout;
  st->poll_interval_ms = poll_interval;
  st->max_wait_ms = max_wait;
  st->discovery_port = discovery_port;
  st->max_mem_read_size = (size_t)max_mem_read;
  if (uia_depth != -1) st->uia_depth = uia_depth;
  st->service_timeout_sec = service_timeout;
  st->max_event_log = (size_t)max_event_log;

  auto backend = std::make_unique<Win32Backend>();
  
  // Propagate config to backend
  json::Object bcfg;
  bcfg["max_mem_read"] = (double)st->max_mem_read_size;
  bcfg["uia_depth"] = (double)st->uia_depth;
  bcfg["service_timeout"] = (double)st->service_timeout_sec;
  backend->set_config(bcfg);

  std::atomic<bool> running{true};

  LOG_INFO("WinInspect Daemon starting up...");
  auto env = backend->get_env_metadata();
  LOG_INFO("Environment: " + env.at("os").as_str() + " (" + env.at("arch").as_str() + ")");
  if (env.count("wine_version")) LOG_INFO("Wine Version: " + env.at("wine_version").as_str());

  // Read auth keys file once at startup (cache content, not path)
  std::string auth_keys_data;
  if (!auth_keys.empty()) {
    std::ifstream kf(auth_keys);
    std::stringstream ks;
    ks << kf.rdbuf();
    auth_keys_data = ks.str();
    LOG_INFO("Loaded " + std::to_string(auth_keys_data.size()) + " bytes of authorized keys from " + auth_keys);
  }

  // 1. Start discovery responder
  LOG_INFO("Starting Discovery responder...");
  std::thread disc_thread([&running, st = st.get(), tcp_port, backend = backend.get(), include_hostname]() {
    run_discovery_responder(&running, st, tcp_port, backend, include_hostname);
  });
  disc_thread.detach();

  // 2. Start cleanup thread
  LOG_INFO("Starting Cleanup thread...");
  std::thread cleanup_thread([&running, st = st.get()]() {
    while (running.load()) {
      Sleep(60000);
      cleanup_sessions(st);
    }
  });
  cleanup_thread.detach();

  // 3. Start Named Pipe server (background)
  LOG_INFO("Starting Named Pipe server (background)...");
  std::thread pipe_thread([&running, st = st.get(), backend = backend.get(),
                            read_only, require_auth, admin_logs, &auth_keys_data]() {
    run_server(&running, st, backend, read_only, require_auth, admin_logs, auth_keys_data);
  });
  pipe_thread.detach();

  // 4. Run TCP server (BLOCKING MAIN THREAD)
  LOG_INFO("Starting TCP Server (blocking main thread)...");
  auto tcp = std::make_shared<wininspectd::TcpServer>(tcp_port, st.get(), backend.get());

  if (!headless) {
    wininspectd::TrayManager tray([&]() {
      LOG_INFO("Shutdown requested via tray.");
      running = false;
      tcp->stop();
      exit(0);
    });
    if (tray.init(GetModuleHandle(nullptr))) {
      // Create a background thread for TCP if tray is running
      std::thread([&, tcp, bind_public, read_only]() {
        try {
          tcp->start(&running, bind_public, auth_keys_data, read_only, admin_logs);
        } catch (...) {}
      }).detach();
      tray.run();
    }
  }

  try {
    tcp->start(&running, bind_public, auth_keys_data, read_only, admin_logs);
  } catch (...) {
    LOG_ERROR("TCP Server fatal error.");
  }

  return 0;
}
#endif
