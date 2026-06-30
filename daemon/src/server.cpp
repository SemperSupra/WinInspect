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
#include "request_handler.hpp"
#include "network_config.hpp"
#include "rendezvous_client.hpp"

#include <list>
#include <set>
#include <future>
#include <memory>
#include <sstream>
#include <fstream>

using namespace wininspect;

namespace {

std::wstring g_pipe_name = L"\\\\.\\pipe\\wininspectd";

void cleanup_sessions(ServerState *st) {
  std::lock_guard<std::mutex> lk(st->snapshots_mu);
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
                   bool read_only, bool require_auth, bool admin_logs, bool no_clipboard,
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
      wininspectd::process_request(m.json, core, st, backend, session,
                      read_only, no_clipboard, require_auth, auth_keys_data,
                      resp, canonical, pinned_sid);
    } catch (...) {
      resp.ok = false;
      resp.error_code = "E_BAD_REQUEST";
    }
  send:
    auto out = serialize_response_json(resp, canonical);
    wininspectd::pipe_write_message(hPipe, out);
    
    // Unpin
    if (!pinned_sid.empty()) {
      std::lock_guard<std::mutex> lk(st->snapshots_mu);
      st->pinned_counts[pinned_sid]--;
    }
  }

  // Unpin any snapshot still pinned from an uncompleted request
  if (!pinned_sid.empty()) {
    std::lock_guard<std::mutex> lk(st->snapshots_mu);
    st->pinned_counts[pinned_sid]--;
  }

  FlushFileBuffers(hPipe);
  DisconnectNamedPipe(hPipe);
  CloseHandle(hPipe);
}

void run_server(std::atomic<bool> *running, ServerState *st,
                IBackend *backend, bool read_only, bool require_auth,
                bool admin_logs, bool no_clipboard, std::string auth_keys_data) {
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
    st->client_threads.emplace_back(handle_client, hPipe, st, backend, read_only, require_auth, admin_logs, no_clipboard, auth_keys_data);
  }
  }
}

void run_discovery_responder(std::atomic<bool> *running, ServerState *st, int tcp_port, IBackend *backend, const NetworkConfig &cfg) {
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

        if (cfg.include_hostname) {
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
  bool read_only = false;
  bool admin_logs = false;
  bool no_clipboard = false;
  bool no_config = false;
  std::string config_path;
  int per_ip_rate_limit_ms = 0;
  std::string auth_keys;
  std::string allow_str, deny_str;
  std::vector<std::string> allow_cidrs, deny_cidrs;
  bool require_auth = false;
  int max_snaps = 1000;
  int max_conns = 32;
  int session_ttl = 3600;
  int poll_interval = 100;
  int max_wait = 30000;
  int max_mem_read = 1024 * 1024;
  int uia_depth = -1;
  int service_timeout = 30;
  int max_event_log = 1000;

  // Parse config path early (others handled by apply_cli_overrides)
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--config" && i + 1 < argc)
      config_path = argv[++i];
    if (std::string(argv[i]) == "--no-config")
      no_config = true;
  }

  // Load config (or use defaults)
  NetworkConfig net_cfg;
  if (!no_config) {
    if (config_path.empty()) config_path = default_config_path();
    net_cfg = load_config(config_path);
  }

  // Ensure identity exists
  auto id = load_or_create_identity(default_config_dir());
  net_cfg.identity = id;

  // Apply CLI overrides (flags override config file)
  net_cfg = wininspectd::apply_cli_overrides(net_cfg, argc, argv);

  // Extract remaining flags not covered by apply_cli_overrides
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--headless")
      headless = true;
    if (std::string(argv[i]) == "--read-only")
      read_only = true;
    if (std::string(argv[i]) == "--allow" && i + 1 < argc)
      allow_str = argv[++i];
    if (std::string(argv[i]) == "--deny" && i + 1 < argc)
      deny_str = argv[++i];
    if (std::string(argv[i]) == "--require-auth")
      require_auth = true;
    if (std::string(argv[i]) == "--admin-logs")
      admin_logs = true;
    if (std::string(argv[i]) == "--no-clipboard")
      no_clipboard = true;
    if (std::string(argv[i]) == "--auth-keys" && i + 1 < argc) {
      auth_keys = argv[++i];
    }
    if (std::string(argv[i]) == "--port" && i + 1 < argc) {
      tcp_port = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--rate-limit-ms" && i + 1 < argc) {
      per_ip_rate_limit_ms = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--allow-ips" && i + 1 < argc) {
      std::string list = argv[++i];
      size_t pos = 0;
      while ((pos = list.find(",")) != std::string::npos) {
        allow_cidrs.push_back(list.substr(0, pos));
        list.erase(0, pos + 1);
      }
      if (!list.empty()) allow_cidrs.push_back(list);
    }
    if (std::string(argv[i]) == "--deny-ips" && i + 1 < argc) {
      std::string list = argv[++i];
      size_t pos = 0;
      while ((pos = list.find(",")) != std::string::npos) {
        deny_cidrs.push_back(list.substr(0, pos));
        list.erase(0, pos + 1);
      }
      if (!list.empty()) deny_cidrs.push_back(list);
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
  st->request_timeout_ms = net_cfg.request_timeout_ms;
  st->poll_interval_ms = poll_interval;
  st->max_wait_ms = max_wait;
  st->discovery_port = net_cfg.discovery_port;
  st->rate_limit_ms = net_cfg.rate_limit_ms;
  st->net_config = net_cfg;
  st->discovery_port = discovery_port;
  st->per_ip_rate_limit_ms = per_ip_rate_limit_ms;
  st->allow_cidrs = allow_cidrs;
  st->deny_cidrs = deny_cidrs;

  // Parse method authorization lists
  if (!allow_str.empty()) {
    size_t pos = 0;
    while ((pos = allow_str.find(",")) != std::string::npos) {
      st->allow_methods.insert(allow_str.substr(0, pos));
      allow_str.erase(0, pos + 1);
    }
    st->allow_methods.insert(allow_str);
  }
  if (!deny_str.empty()) {
    size_t pos = 0;
    while ((pos = deny_str.find(",")) != std::string::npos) {
      st->deny_methods.insert(deny_str.substr(0, pos));
      deny_str.erase(0, pos + 1);
    }
    st->deny_methods.insert(deny_str);
  }
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
  LOG_INFO("Instance: " + net_cfg.identity.uuid + " (" + net_cfg.identity.name + ")");
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
  std::thread disc_thread([&running, st = st.get(), backend = backend.get(), &net_cfg]() {
    run_discovery_responder(&running, st, net_cfg.port, backend, net_cfg);
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
                            read_only, require_auth, admin_logs, no_clipboard, &auth_keys_data]() {
    run_server(&running, st, backend, read_only, require_auth, admin_logs, no_clipboard, auth_keys_data);
  });
  pipe_thread.detach();

  // 4. Auto-update checker (background)
  if (net_cfg.enable_update_check) {
    LOG_INFO("Auto-update checker enabled (every " + std::to_string(net_cfg.update_check_interval_hours) + "h)");
    std::thread update_thread([&running, backend = backend.get(), &net_cfg]() {
      while (running.load()) {
        auto info = backend->check_for_update();
        if (info.update_available) {
          LOG_INFO("Update available: " + info.latest_version + " (current: " + info.current_version + ")");
        }
        Sleep(net_cfg.update_check_interval_hours * 3600 * 1000);
      }
    });
    update_thread.detach();
  }

  // 5. Rendezvous registration + heartbeat
  for (auto &rv_cfg : net_cfg.rendezvous) {
    auto rv_client = wininspectd::create_rendezvous_client(rv_cfg);
    if (rv_client) {
      if (rv_client->register_instance(net_cfg.identity, "", net_cfg.port)) {
        LOG_INFO("Registered with rendezvous: " + rv_cfg.url);
        std::thread rv_thread([&running, c = std::move(rv_client)]() {
          while (running.load()) {
            Sleep(c->heartbeat() ? 30000 : 10000);
          }
          c->deregister();
        });
        rv_thread.detach();
      }
    }
  }

  // 6. Run TCP server (BLOCKING MAIN THREAD)
  LOG_INFO("Starting TCP Server (blocking main thread)...");
  auto tcp = std::make_shared<wininspectd::TcpServer>(st.get(), backend.get());

  if (!headless) {
    wininspectd::TrayManager tray([&]() {
      LOG_INFO("Shutdown requested via tray.");
      running = false;
      tcp->stop();
      exit(0);
    });
    if (tray.init(GetModuleHandle(nullptr))) {
      std::thread([&, tcp, &auth_keys_data, read_only]() {
        try {
          tcp->start(&running, net_cfg, auth_keys_data, read_only, admin_logs, no_clipboard);
          tcp->start(&running, bind_public, auth_keys_data, read_only, admin_logs, no_clipboard, 0);
        } catch (...) {}
      }).detach();
      tray.run();
    }
  }

  try {
    tcp->start(&running, net_cfg, auth_keys_data, read_only, admin_logs, no_clipboard);
    tcp->start(&running, bind_public, auth_keys_data, read_only, admin_logs, no_clipboard, 0);
  } catch (...) {
    LOG_ERROR("TCP Server fatal error.");
  }

  return 0;
}
#endif
