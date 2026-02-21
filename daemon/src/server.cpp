#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
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

using namespace wininspect;

namespace {


const wchar_t *PIPE_NAME = L"\\\\.\\pipe\\wininspectd";

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
                   bool read_only, const std::string& auth_keys_u8) {
  CoInitGuard coinit;
  CoreEngine core(backend);
  ClientSession session;
  st->active_connections++;
  LOG_INFO("New client connection established.");

  // Handle auto-auth for local pipes if no keys set
  if (auth_keys_u8.empty()) {
    session.authenticated = true;
    LOG_DEBUG("Local auto-auth enabled (no keys).");
  }

  // Ensure decrement on exit
  struct ConnGuard {
    std::atomic<int>& count;
    ~ConnGuard() { 
      count--; 
      LOG_INFO("Client connection closed.");
    }
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

  FlushFileBuffers(hPipe);
  DisconnectNamedPipe(hPipe);
  CloseHandle(hPipe);
}

void run_server(std::atomic<bool> *running, ServerState *st,
                IBackend *backend, bool read_only, std::string auth_keys_u8) {
  LOG_INFO("Named Pipe server thread starting...");
  while (running->load()) {
    HANDLE hPipe = CreateNamedPipeW(
        PIPE_NAME, PIPE_ACCESS_DUPLEX,
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

    std::thread(handle_client, hPipe, st, backend, read_only, auth_keys_u8).detach();
  }
}

void run_discovery_responder(std::atomic<bool> *running, ServerState *st, int tcp_port, IBackend *backend) {
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
        
        char hostname_buf[256];
        gethostname(hostname_buf, sizeof(hostname_buf));
        resp["hostname"] = std::string(hostname_buf);

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
  std::string auth_keys;
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

  std::string auth_keys_u8 = auth_keys;

  // 1. Start discovery responder
  LOG_INFO("Starting Discovery responder...");
  struct DiscoveryArgs { std::atomic<bool>* running; ServerState* st; int port; IBackend* backend; };
  auto d_args = new DiscoveryArgs{&running, st.get(), tcp_port, backend.get()};
  CreateThread(NULL, 0, [](LPVOID p) -> DWORD {
    auto a = (DiscoveryArgs*)p;
    run_discovery_responder(a->running, a->st, a->port, a->backend);
    delete a;
    return 0;
  }, d_args, 0, NULL);

  // 2. Start cleanup thread
  LOG_INFO("Starting Cleanup thread...");
  struct CleanupArgs { ServerState* st; std::atomic<bool>* running; };
  auto c_args = new CleanupArgs{st.get(), &running};
  CreateThread(NULL, 0, [](LPVOID p) -> DWORD {
    auto a = (CleanupArgs*)p;
    while (a->running->load()) {
      Sleep(60000);
      cleanup_sessions(a->st);
    }
    delete a;
    return 0;
  }, c_args, 0, NULL);

  // 3. Start Named Pipe server (background)
  LOG_INFO("Starting Named Pipe server (background)...");
  struct ServerArgs { std::atomic<bool>* running; ServerState* st; IBackend* backend; bool ro; std::string keys; };
  auto s_args = new ServerArgs{&running, st.get(), backend.get(), read_only, auth_keys_u8};
  CreateThread(NULL, 0, [](LPVOID p) -> DWORD {
    auto a = (ServerArgs*)p;
    run_server(a->running, a->st, a->backend, a->ro, a->keys);
    delete a;
    return 0;
  }, s_args, 0, NULL);

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
      std::thread([&, tcp, bind_public, auth_keys_u8, read_only]() {
        try {
          tcp->start(&running, bind_public, auth_keys_u8, read_only);
        } catch (...) {}
      }).detach();
      tray.run();
    }
  }

  try {
    tcp->start(&running, bind_public, auth_keys_u8, read_only);
  } catch (...) {
    LOG_ERROR("TCP Server fatal error.");
  }

  return 0;
}
#endif
