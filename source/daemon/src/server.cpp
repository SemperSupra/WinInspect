// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#ifdef _WIN32
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <windows.h>
#include <sddl.h>
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
#include "http_server.hpp"
#include "indicator_manager.hpp"
#include "wininspect/mdns.hpp"
#include "wininspect/tailscale.hpp"
#include "wininspect/tls.hpp"
#include "rendezvous_client.hpp"

#include <list>
#include <set>
#include <future>
#include <memory>
#include <sstream>
#include <fstream>
#include <iostream>

using namespace wininspect;

namespace {

  static std::optional<std::string> get_str(const json::Object& o, const std::string& k)
  {
    auto it = o.find(k);
    if (it != o.end() && it->second.is_str())
      return it->second.as_str();
    return std::nullopt;
  }
  static std::optional<double> get_num(const json::Object& o, const std::string& k)
  {
    auto it = o.find(k);
    if (it != o.end() && it->second.is_num())
      return it->second.as_num();
    return std::nullopt;
  }

  std::wstring g_pipe_name = L"\\\\.\\pipe\\wininspectd";

  void cleanup_sessions(ServerState* st)
  {
    // Clean stale sessions
    {
      std::lock_guard<std::mutex> lk(st->snapshots_mu);
      auto now = std::chrono::steady_clock::now();
      for (auto it = st->sessions.begin(); it != st->sessions.end();) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_activity)
                .count();
        if (elapsed > st->session_ttl_sec) {
          it = st->sessions.erase(it);
        }
        else {
          ++it;
        }
      }
    }
    // Sweep finished client threads (handle leak prevention)
    {
      std::lock_guard<std::mutex> lk(st->thread_mu);
      for (auto it = st->client_threads.begin(); it != st->client_threads.end();) {
        if (it->done->load()) {
          if (it->t.joinable())
            it->t.join();
          it = st->client_threads.erase(it);
        }
        else {
          ++it;
        }
      }
    }
  }

  void handle_client(HANDLE hPipe, ServerState* st, IBackend* backend, bool read_only,
                     bool require_auth, bool admin_logs, bool no_clipboard,
                     const std::string& auth_keys_data, bool audit_all)
  {
    CoInitGuard coinit;
    CoreEngine core(backend);
    core.set_admin_logs_enabled(admin_logs);
    core.set_read_only(read_only);
    core.set_daemon_state(ServerState::state_str(st->daemon_state));
    core.set_license_info(st->net_config.deployment, st->net_config.license_type);
    if (audit_all && st->control) {
      core.set_audit_hook([st](const CoreRequest& req, const CoreResponse& resp) {
        st->control->log_action(req.method, req.params, resp.ok, 0);
      });
    }
    ClientSession session;
    st->active_connections++;
    LOG_INFO("New client connection established.");

    // Auto-auth local pipes only when not in require-auth mode and no keys configured
    if (!require_auth && auth_keys_data.empty()) {
      session.authenticated = true;
      LOG_DEBUG("Local auto-auth enabled (no keys, not require-auth).");
    }

    // Ensure decrement on exit
    struct ConnGuard
    {
      std::atomic<int>& count;
      ~ConnGuard()
      {
        count--;
        LOG_INFO("Client connection closed.");
      }
    } guard{st->active_connections};

    wininspect::PinGuard pin_guard;
    bool version_checked = true;     // pipe clients always current
    bool compress_responses = false; // pipe transport, no compression
    while (true) {
      wininspectd::PipeMessage m;
      if (!wininspectd::pipe_read_message(hPipe, m))
        break;

      CoreResponse resp;
      bool canonical = false;
      bool close_connection = false;
      pin_guard.clear();

      // ── Process request via shared handler ──────────────────────────
      // Same process_request() used by TCP — handles session, snapshot,
      try {
        // events, control methods, dispatch. Eliminates duplication.
        wininspectd::process_request(m.json, core, st, backend, session, read_only, no_clipboard,
                                     require_auth, auth_keys_data, version_checked,
                                     compress_responses, resp, canonical, pin_guard,
                                     close_connection);
      }
      catch (...) {
        resp.ok = false;
        resp.error_code = "E_BAD_REQUEST";
      }
      auto out = serialize_response_json(resp, canonical);
      if (out.size() > st->max_response_size) {
        resp.ok = false;
        resp.error_code = "E_RESPONSE_TOO_LARGE";
        resp.error_message = "response exceeds " + std::to_string(st->max_response_size) + " bytes";
        out = serialize_response_json(resp, canonical);
      }
      wininspectd::pipe_write_message(hPipe, out);
      // PinGuard handles unpin automatically at end of scope
    }

    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
  }

  void run_server(std::atomic<bool>* running, ServerState* st, IBackend* backend, bool read_only,
                  bool require_auth, bool admin_logs, bool no_clipboard, std::string auth_keys_data,
                  bool audit_all)
  {
    std::string pipe_name_narrow(g_pipe_name.begin(), g_pipe_name.end());
    LOG_INFO("Named Pipe server starting on: " + pipe_name_narrow);
    while (running->load()) {
      // Check capacity BEFORE creating the pipe to avoid wasted overhead
      if (st->active_connections >= st->max_connections) {
        Sleep(100); // throttle the reject loop
        continue;
      }

      // Simplified named pipe for Wine compatibility:
      // no security descriptor (SDDL not reliable under Wine),
      // blocking WAIT mode (NOWAIT not supported on all Wine versions).
      HANDLE hPipe = CreateNamedPipeW(g_pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
                                      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                      PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, nullptr);

      if (hPipe == INVALID_HANDLE_VALUE) {
        LOG_ERROR("Failed to create Named Pipe: " + std::to_string(GetLastError()));
        break;
      }

      DWORD waitStart = GetTickCount();
      while (true) {
        BOOL ok = ConnectNamedPipe(hPipe, nullptr);
        if (ok)
          break; // Client connected
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED)
          break; // Client connected between Create and Connect
        if (err != ERROR_PIPE_LISTENING) {
          CloseHandle(hPipe);
          break;
        } // Real error
        // No client yet — check shutdown and retry
        if (!running->load()) {
          CloseHandle(hPipe);
          break;
        }
        // 5 second timeout on pipe wait to prevent hang during shutdown
        if (GetTickCount() - waitStart > 5000) {
          CloseHandle(hPipe);
          break;
        }
        Sleep(50);
      }

      LOG_DEBUG("Named Pipe connection accepted.");

      {
        std::lock_guard<std::mutex> lk(st->thread_mu);
        ThreadHandle th;
        th.t = std::thread([hPipe, st, backend, read_only, require_auth, admin_logs, no_clipboard,
                            auth_keys_data, audit_all, done = th.done]() {
          handle_client(hPipe, st, backend, read_only, require_auth, admin_logs, no_clipboard,
                        auth_keys_data, audit_all);
          *done = true;
        });
        st->client_threads.push_back(std::move(th));
      }
    }
  }

  void run_discovery_responder(std::atomic<bool>* running, ServerState* st, int tcp_port,
                               IBackend* backend, const NetworkConfig& cfg)
  {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
      return;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)st->discovery_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    // Set receive timeout so the recvfrom loop can check the running flag
    DWORD rcv_timeout = 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcv_timeout, sizeof(rcv_timeout));

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
      closesocket(s);
      return;
    }

    LOG_INFO("Discovery responder listening on UDP " + std::to_string(st->discovery_port));

    while (running->load()) {
      char buf[512];
      struct sockaddr_in client_addr;
      int client_addr_len = sizeof(client_addr);
      int r = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr*)&client_addr, &client_addr_len);
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
          resp["pipe_name"] =
              (last_bs != std::string::npos) ? full_pipe.substr(last_bs + 1) : full_pipe;

          if (cfg.include_hostname) {
            char hostname_buf[256];
            gethostname(hostname_buf, sizeof(hostname_buf));
            resp["hostname"] = std::string(hostname_buf);
          }
          if (!cfg.tailscale_ip.empty()) {
            resp["tailscale_ip"] = cfg.tailscale_ip;
          }

          std::string out = json::dumps(resp);
          sendto(s, out.data(), (int)out.size(), 0, (struct sockaddr*)&client_addr,
                 client_addr_len);
        }
      }
    }
    closesocket(s);
  }

} // namespace

int main(int argc, char** argv)
{
  bool headless = false;
  bool read_only = false;
  bool admin_logs = false;
  bool no_clipboard = false;
  bool no_config = false;
  bool audit_all = false;
  std::string config_path;
  std::string auth_keys;
  std::string http_token;
  std::string https_cert_pem;
  std::string https_key_pem;
  std::string cert_pem_str; // Shared cert PEM for HTTPS + TLS TCP (populated below)
  std::string key_pem_str;  // Shared key PEM for HTTPS + TLS TCP
  std::string allow_str, deny_str;
  bool require_auth = false;
  int max_snaps = 1000;
  int max_conns = 32;
  int max_sessions = 256;
  int max_audit_entries = 10000;
  int session_ttl = 3600;
  int poll_interval = 100;
  int max_wait = 30000;
  int max_mem_read = 1024 * 1024;
  int uia_depth = -1;
  int service_timeout = 30;
  int max_event_log = 1000;
  int max_response_size = 64 * 1024 * 1024;

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
    if (config_path.empty())
      config_path = default_config_path();
    net_cfg = load_config(config_path);
  }

  // Ensure identity exists
  auto id = load_or_create_identity(default_config_dir());
  net_cfg.identity = id;

  // Detect Tailscale IP for cross-subnet discovery (works on Windows + Wine)
  auto tailscale_ip_addr = wininspect::tailscale_ip();
  if (tailscale_ip_addr) {
    LOG_INFO("Tailscale IP detected: " + *tailscale_ip_addr);
    net_cfg.tailscale_ip = *tailscale_ip_addr;
  }

  // Apply CLI overrides (flags override config file)
  net_cfg = wininspectd::apply_cli_overrides(net_cfg, argc, argv);

  // Handle --dump-config (print effective config and exit)
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--dump-config") {
      std::cout << json::dumps(net_cfg.to_json()) << std::endl;
      return 0;
    }
  }

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
    if (std::string(argv[i]) == "--audit-all")
      audit_all = true;
    if (std::string(argv[i]) == "--auth-keys" && i + 1 < argc) {
      auth_keys = argv[++i];
    }
    if (std::string(argv[i]) == "--max-snapshots" && i + 1 < argc) {
      max_snaps = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--max-conns" && i + 1 < argc) {
      max_conns = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--max-sessions" && i + 1 < argc) {
      max_sessions = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--max-audit-entries" && i + 1 < argc) {
      max_audit_entries = std::stoi(argv[++i]);
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
    if (std::string(argv[i]) == "--http-token" && i + 1 < argc) {
      http_token = argv[++i];
    }
    if (std::string(argv[i]) == "--https-cert" && i + 1 < argc) {
      https_cert_pem = argv[++i];
    }
    if (std::string(argv[i]) == "--https-key" && i + 1 < argc) {
      https_key_pem = argv[++i];
    }
    if (std::string(argv[i]) == "--max-response-size" && i + 1 < argc) {
      max_response_size = std::stoi(argv[++i]);
    }
    if (std::string(argv[i]) == "--log-file" && i + 1 < argc) {
      Logger::get().set_log_file(argv[++i]);
    }
    if (std::string(argv[i]) == "--log-level" && i + 1 < argc) {
      std::string lvl = argv[++i];
      if (lvl == "TRACE")
        Logger::get().set_level(LogLevel::TRACE);
      else if (lvl == "DEBUG")
        Logger::get().set_level(LogLevel::DEBUG);
      else if (lvl == "INFO")
        Logger::get().set_level(LogLevel::INFO);
      else if (lvl == "WARN")
        Logger::get().set_level(LogLevel::WARN);
      else if (lvl == "ERROR")
        Logger::get().set_level(LogLevel::ERR);
    }
  }

  auto st = std::make_unique<ServerState>();
  st->max_snapshots = (size_t)max_snaps;
  st->max_connections = max_conns;
  st->max_sessions = (size_t)max_sessions;
  if (st->control)
    st->control->set_max_entries((size_t)max_audit_entries);
  st->session_ttl_sec = session_ttl;
  st->request_timeout_ms = net_cfg.request_timeout_ms;
  st->poll_interval_ms = poll_interval;
  st->max_wait_ms = max_wait;
  st->discovery_port = net_cfg.discovery_port;
  st->rate_limit_ms = net_cfg.rate_limit_ms;
  st->net_config = net_cfg;
  st->control = std::make_unique<wininspectd::ControlManager>();
  // Load persisted audit log
  std::string audit_log_path = default_config_dir() + "/audit.jsonl";
  st->control->load_audit_log(audit_log_path);

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
  if (uia_depth != -1)
    st->uia_depth = uia_depth;
  st->service_timeout_sec = service_timeout;
  st->max_event_log = (size_t)max_event_log;
  st->max_response_size = (size_t)max_response_size;

  auto backend = std::make_unique<Win32Backend>();

  // Propagate config to backend
  json::Object bcfg;
  bcfg["max_mem_read"] = (double)st->max_mem_read_size;
  bcfg["uia_depth"] = (double)st->uia_depth;
  bcfg["service_timeout"] = (double)st->service_timeout_sec;
  backend->set_config(bcfg);

  auto running = std::make_shared<std::atomic<bool>>(true);

  LOG_INFO("WinInspect Daemon " + std::string(wininspect::WININSPECT_VERSION) +
           " starting up — window inspection for Windows and Wine");
  auto env = backend->get_env_metadata();
  LOG_INFO("Instance: " + net_cfg.identity.uuid + " (" + net_cfg.identity.name + ")");
  LOG_INFO("License: " + net_cfg.license_type + " · Deployment: " + net_cfg.deployment);
  // Auto-create log directory in config dir (if --log-file not specified)
  Logger::get().set_log_dir(default_config_dir() + "/logs");
  LOG_INFO("Environment: " + env.at("os").as_str() + " (" + env.at("arch").as_str() + ")");
  if (env.count("wine_version"))
    LOG_INFO("Wine Version: " + env.at("wine_version").as_str());

  // Read auth keys file once at startup (cache content, not path)
  std::string auth_keys_data;
  if (!auth_keys.empty()) {
    std::ifstream kf(auth_keys);
    std::stringstream ks;
    ks << kf.rdbuf();
    auth_keys_data = ks.str();
    LOG_INFO("Loaded " + std::to_string(auth_keys_data.size()) + " bytes of authorized keys from " +
             auth_keys);
  }

  // ── Background Threads ──────────────────────────────────────────────────────
  std::vector<std::thread> bg_threads;

  // Transition to starting state
  st->daemon_state = ServerState::State::Starting;

  // 1. Start discovery responder (if enabled)
  if (net_cfg.enable_discovery) {
    LOG_INFO("Starting Discovery responder...");
    bg_threads.emplace_back([running, st = st.get(), backend = backend.get(), &net_cfg]() {
      run_discovery_responder(running.get(), st, net_cfg.port, backend, net_cfg);
    });
  }
  else {
    LOG_INFO("Discovery responder disabled (--no-discovery)");
  }

  // 2. Start cleanup thread
  LOG_INFO("Starting Cleanup thread (every " + std::to_string(net_cfg.cleanup_interval_ms / 1000) +
           "s)...");
  auto health_ok = std::make_shared<std::atomic<bool>>(true);
  bg_threads.emplace_back([running, st = st.get(), health_ok, &net_cfg]() {
    while (running->load()) {
      Sleep(net_cfg.cleanup_interval_ms);
      cleanup_sessions(st);
      *health_ok = running->load();
    }
  });

  // 3. Start Named Pipe server (background)
  // Try-catch wrapper: named pipes may fail or crash under Wine,
  // but the daemon should continue with TCP-only service.
  LOG_INFO("Starting Named Pipe server (background)...");
  bg_threads.emplace_back([running, st = st.get(), backend = backend.get(), read_only, require_auth,
                           admin_logs, no_clipboard, auth_keys_data, audit_all]() {
    try {
      run_server(running.get(), st, backend, read_only, require_auth, admin_logs, no_clipboard,
                 auth_keys_data, audit_all);
    }
    catch (const std::exception& e) {
      LOG_WARN("Named Pipe server failed (continuing with TCP): " + std::string(e.what()));
    }
    catch (...) {
      LOG_WARN("Named Pipe server crashed (continuing with TCP)");
    }
  });

  // Shared update state (set by update thread, read by tray)
  auto update_state = std::make_shared<wininspectd::TrayManager::UpdateState>();
  std::shared_ptr<HWND> tray_hwnd = std::make_shared<HWND>(nullptr);

  // 4. Auto-update checker (background)
  if (net_cfg.enable_update_check) {
    LOG_INFO("Auto-update checker enabled (every " +
             std::to_string(net_cfg.update_check_interval_hours) + "h)");
    bg_threads.emplace_back(
        [running, backend = backend.get(), &net_cfg, update_state, tray_hwnd]() {
          while (running->load()) {
            auto info = backend->check_for_update();
            if (info.update_available && !update_state->available) {
              update_state->available = true;
              update_state->latest_version = info.latest_version;
              update_state->release_notes = info.release_notes;
              LOG_INFO("Update available: " + info.latest_version +
                       " (current: " + info.current_version + ")");
              // Post message to tray window if it exists
              HWND hwnd = *tray_hwnd;
              if (hwnd) {
                PostMessageW(hwnd, wininspectd::TrayManager::WM_UPDATE_AVAILABLE, 0, 0);
              }
            }
            Sleep(net_cfg.update_check_interval_hours * 3600 * 1000);
          }
        });
  }

  // 5. Rendezvous registration + heartbeat
  for (auto& rv_cfg : net_cfg.rendezvous) {
    auto rv_client = wininspectd::create_rendezvous_client(rv_cfg);
    if (!rv_client) {
      LOG_WARN("Failed to create rendezvous client for: " + rv_cfg.url);
      continue;
    }
    if (rv_client->register_instance(net_cfg.identity, "", net_cfg.port)) {
      LOG_INFO("Registered with rendezvous: " + rv_cfg.url);
      bg_threads.emplace_back([running, c = std::move(rv_client)]() {
        int heartbeat_fails = 0;
        constexpr int MAX_HEARTBEAT_FAILS = 10;
        while (running->load()) {
          if (!c->heartbeat()) {
            heartbeat_fails++;
            if (heartbeat_fails >= MAX_HEARTBEAT_FAILS) {
              LOG_WARN("Rendezvous heartbeat failed " + std::to_string(heartbeat_fails) +
                       " times — giving up");
              break;
            }
            Sleep(10000);
          }
          else {
            heartbeat_fails = 0;
            Sleep(30000);
          }
        }
        c->deregister();
      });
    }
    else {
      LOG_WARN("Rendezvous registration FAILED for: " + rv_cfg.url);
    }
  }

  // 5b. Start mDNS responder (if enabled)
  if (net_cfg.enable_mdns) {
    LOG_INFO("Starting mDNS responder...");
    bg_threads.emplace_back([running, &net_cfg, backend = backend.get()]() {
      char hostname_buf[256] = {};
      gethostname(hostname_buf, sizeof(hostname_buf));
      wininspect::MdnsResponder mdns;
      mdns.start(running.get(), "wininspect", net_cfg.port, std::string(hostname_buf));
    });
  }
  else {
    LOG_INFO("mDNS responder disabled (--no-mdns)");
  }

  // 5c. Start HTTP server (if --http-port set)
  if (net_cfg.http_port > 0) {
    LOG_INFO("Starting HTTP server on port " + std::to_string(net_cfg.http_port) + "...");
    bg_threads.emplace_back([running, backend = backend.get(), &net_cfg, read_only, no_clipboard,
                             http_token, st = st.get(), audit_all]() {
      wininspect::CoreEngine http_core(backend);
      http_core.set_read_only(read_only);
      http_core.set_license_info(net_cfg.deployment, net_cfg.license_type);
      if (audit_all && st->control) {
        http_core.set_audit_hook([st](const CoreRequest& req, const CoreResponse& resp) {
          st->control->log_action(req.method, req.params, resp.ok, 0);
        });
      }
      wininspectd::run_http_server(running.get(), net_cfg.http_port, http_core,
                                   &http_core.metrics_collector_, st, http_token,
                                   read_only, no_clipboard, net_cfg.https_port);
    });
  }

  // 5d. Start HTTPS server (TLS 1.3)
  if (net_cfg.https_port > 0) {
    bool use_auto_cert = false;

    if (!https_cert_pem.empty() && !https_key_pem.empty()) {
      std::ifstream cf(https_cert_pem, std::ios::binary);
      std::ifstream kf(https_key_pem, std::ios::binary);
      if (cf && kf) {
        cert_pem_str.assign((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
        key_pem_str.assign((std::istreambuf_iterator<char>(kf)), std::istreambuf_iterator<char>());
      }
    }

    if (cert_pem_str.empty() || key_pem_str.empty()) {
      std::string subject = net_cfg.identity.name.empty()
                                ? "WinInspect-" + net_cfg.identity.uuid.substr(0, 8)
                                : net_cfg.identity.name;
      LOG_INFO("HTTPS: generating self-signed certificate for " + subject);
      if (!wininspect::TlsSession::generate_self_signed_cert(subject, cert_pem_str, key_pem_str)) {
        LOG_ERROR("HTTPS: failed to generate self-signed certificate");
        cert_pem_str.clear();
        key_pem_str.clear();
      }
      else {
        use_auto_cert = true;
      }
    }

    if (!cert_pem_str.empty() && !key_pem_str.empty()) {
      LOG_INFO("Starting HTTPS server on port " + std::to_string(net_cfg.https_port) +
               (use_auto_cert ? " (self-signed cert)" : " (user-provided cert)"));
      bg_threads.emplace_back([running, backend = backend.get(), &net_cfg, read_only, no_clipboard,
                               http_token, cert_pem_str, key_pem_str]() {
        wininspect::CoreEngine https_core(backend);
        https_core.set_license_info(net_cfg.deployment, net_cfg.license_type);
        wininspectd::run_https_server(running.get(), net_cfg.https_port, https_core, http_token,
                                      read_only, no_clipboard, cert_pem_str, key_pem_str);
      });
    }
    else {
      LOG_ERROR("HTTPS: no certificate available, HTTPS server not started");
    }
  }

  // 6. TCP server
  auto tcp = std::make_shared<wininspectd::TcpServer>(st.get(), backend.get());

  // Start TLS-wrapped TCP listener on a background thread (if configured)
  if (net_cfg.tls_port > 0 && !cert_pem_str.empty() && !key_pem_str.empty()) {
    LOG_INFO("Starting TLS TCP server on port " + std::to_string(net_cfg.tls_port) + "...");
    bg_threads.emplace_back([running, tcp, &net_cfg, auth_keys_data, read_only, admin_logs,
                             no_clipboard, cert_pem_str, key_pem_str]() {
      tcp->start_tls(running.get(), net_cfg, cert_pem_str, key_pem_str, auth_keys_data, read_only,
                     admin_logs, no_clipboard);
    });
  }

  // Transition to running state before blocking
  st->daemon_state = ServerState::State::Running;

  if (!headless) {
    // Tray mode: start TCP in background, run message loop
    LOG_INFO("Starting TCP Server (background) for tray mode...");
    bg_threads.emplace_back(
        [running, tcp, &net_cfg, auth_keys_data, read_only, admin_logs, no_clipboard]() {
          try {
            tcp->start(running.get(), net_cfg, auth_keys_data, read_only, admin_logs, no_clipboard);
          }
          catch (...) {
          }
        });
    wininspectd::TrayManager tray([running = running, st = st.get(), tcp = tcp]() {
      LOG_INFO("Shutdown requested via tray.");
      *running = false;
      tcp->stop();
    });
    if (tray.init(GetModuleHandle(nullptr))) {
      *tray_hwnd = tray.get_hwnd();
      tray.set_health_flag(health_ok);
      tray.set_status_callbacks(
          [st = st.get()]() -> std::string {
            if (st && st->control)
              return wininspect::controller_type_str(st->control->current_controller());
            return "none";
          },
          [st = st.get()]() -> int {
            return st ? st->active_connections.load() : 0;
          });

      // Optional control indicators (tray badge, cursor, border, audio)
      auto indicator = std::make_shared<wininspectd::IndicatorManager>();
      indicator->init(GetModuleHandle(nullptr), tray.get_hwnd());
      tray.set_indicator_callback([indicator, st = st.get(), backend = backend.get()]() {
        if (st && st->control) {
          // Detect local human input (keyboard/mouse) and auto-release agent
          if (backend->poll_local_input())
            st->control->notify_local_input();
          indicator->update(st->control->current_controller());
        }
      });

      tray.run(); // Blocks until tray exits
    }
  }
  else {
    // Headless mode: start TCP on main thread (blocking)
    LOG_INFO("Starting TCP Server (blocking main thread)...");
    try {
      tcp->start(running.get(), net_cfg, auth_keys_data, read_only, admin_logs, no_clipboard);
    }
    catch (...) {
      LOG_ERROR("TCP Server fatal error.");
    }
  }

  // Signal shutdown and wait for background threads to finish
  st->daemon_state = ServerState::State::Draining;
  *running = false;
  // Persist audit log to disk
  st->control->save_audit_log(audit_log_path);
  LOG_INFO("Shutting down background threads...");
  for (auto& t : bg_threads) {
    if (t.joinable())
      t.join();
  }
  LOG_INFO("Daemon shutdown complete.");
  return 0;
}
#endif
