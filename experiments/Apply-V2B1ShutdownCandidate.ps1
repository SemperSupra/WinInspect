$ErrorActionPreference = 'Stop'

function Replace-Exactly {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Old,
        [Parameter(Mandatory=$true)][string]$New
    )

    $text = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $oldNorm = $Old.Replace("`r`n", "`n")
    $newNorm = $New.Replace("`r`n", "`n")
    $first = $text.IndexOf($oldNorm, [StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Expected source block not found in $Path" }
    $second = $text.IndexOf($oldNorm, $first + $oldNorm.Length, [StringComparison]::Ordinal)
    if ($second -ge 0) { throw "Expected source block occurs more than once in $Path" }
    $updated = $text.Substring(0, $first) + $newNorm + $text.Substring($first + $oldNorm.Length)
    [IO.File]::WriteAllText($Path, $updated, [Text.UTF8Encoding]::new($false))
}

# CoreEngine requests shutdown but never owns process/thread/transport teardown.
Replace-Exactly 'source/core/include/wininspect/core.hpp' @'
    using AuditHook = std::function<void(const CoreRequest&, const CoreResponse&)>;
    void set_audit_hook(AuditHook hook) { audit_hook_ = std::move(hook); }

    // Daemon lifecycle state (set by the daemon main(), read by daemon.status)
'@ @'
    using AuditHook = std::function<void(const CoreRequest&, const CoreResponse&)>;
    void set_audit_hook(AuditHook hook) { audit_hook_ = std::move(hook); }

    // Core can request shutdown, but the daemon remains the sole owner of
    // transport/process/thread lifecycle. The hook marks the request only;
    // transport code activates the run-gate after the response is written.
    using ShutdownHook = std::function<void()>;
    void set_shutdown_hook(ShutdownHook hook) { shutdown_hook_ = std::move(hook); }

    // Daemon lifecycle state (set by the daemon main(), read by daemon.status)
'@

Replace-Exactly 'source/core/include/wininspect/core.hpp' @'
  private:
    AuditHook audit_hook_;
    std::string daemon_state_ = "init";
'@ @'
  private:
    AuditHook audit_hook_;
    ShutdownHook shutdown_hook_;
    std::string daemon_state_ = "init";
'@

Replace-Exactly 'source/core/src/core.cpp' @'
    dispatch_["daemon.shutdown"] = [](const CoreRequest&, const Snapshot&, const Snapshot*) {
      // Signal the daemon to shut down gracefully.
      // We schedule exit on a thread so the RPC response is sent first.
      CoreResponse resp;
      resp.ok = true;
      resp.result = json::Object{{"ok", true}};
      std::thread([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::exit(0);
      }).detach();
      return resp;
    };
'@ @'
    dispatch_["daemon.shutdown"] = [this](const CoreRequest&, const Snapshot&, const Snapshot*) {
      CoreResponse resp;
      if (!shutdown_hook_) {
        resp.ok = false;
        resp.error_code = "E_SHUTDOWN_UNAVAILABLE";
        resp.error_message = "daemon lifecycle hook is not installed";
        return resp;
      }

      // Mark the daemon-owned lifecycle request now. The transport activates
      // the shared run gate only after this response has been written.
      resp.ok = true;
      resp.result = json::Object{{"ok", true}};
      shutdown_hook_();
      return resp;
    };
'@

# ServerState owns the two-phase handoff and client cancellation descriptors.
Replace-Exactly 'source/daemon/src/server_state.hpp' @'
#include <thread>
#include <vector>
#include <set>
#include <memory>
'@ @'
#include <thread>
#include <vector>
#include <set>
#include <functional>
#include <memory>
'@

Replace-Exactly 'source/daemon/src/server_state.hpp' @'
struct ThreadHandle
{
  std::thread t;
  std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
};
'@ @'
struct ThreadHandle
{
  std::thread t;
  std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
  std::function<void()> cancel;
};
'@

Replace-Exactly 'source/daemon/src/server_state.hpp' @'
    State daemon_state = State::Init;
    std::mutex daemon_state_mu; // protects daemon_state

    /// Convert DaemonState to string for API responses (daemon.status)
'@ @'
    State daemon_state = State::Init;
    std::mutex daemon_state_mu; // protects daemon_state

    // Two-phase shutdown handshake. The RPC marks shutdown_requested while
    // the transport still owns the response. After the response is written,
    // activate_shutdown clears the shared run gate.
    std::shared_ptr<std::atomic<bool>> running;
    std::atomic<bool> shutdown_requested{false};

    void request_shutdown()
    {
      {
        std::lock_guard<std::mutex> lk(daemon_state_mu);
        if (daemon_state != State::Stopped && daemon_state != State::Failed)
          daemon_state = State::Draining;
      }
      shutdown_requested.store(true);
    }

    void activate_shutdown()
    {
      if (!shutdown_requested.load())
        return;
      if (running)
        running->store(false);
    }

    bool shutdown_pending() const
    {
      return shutdown_requested.load();
    }

    /// Convert DaemonState to string for API responses (daemon.status)
'@

# Named-pipe transport installs the lifecycle hook, acknowledges first, then activates shutdown.
Replace-Exactly 'source/daemon/src/server.cpp' @'
    core.set_read_only(read_only);
    core.set_daemon_state(ServerState::state_str(st->daemon_state));
    core.set_license_info(st->net_config.deployment, st->net_config.license_type);
    if (audit_all && st->control) {
'@ @'
    core.set_read_only(read_only);
    core.set_daemon_state(ServerState::state_str(st->daemon_state));
    core.set_license_info(st->net_config.deployment, st->net_config.license_type);
    core.set_shutdown_hook([st]() { st->request_shutdown(); });
    if (audit_all && st->control) {
'@

Replace-Exactly 'source/daemon/src/server.cpp' @'
      wininspectd::pipe_write_message(hPipe, out);
      // PinGuard handles unpin automatically at end of scope
'@ @'
      wininspectd::pipe_write_message(hPipe, out);
      if (st->shutdown_pending()) {
        st->activate_shutdown();
        break;
      }
      // PinGuard handles unpin automatically at end of scope
'@

Replace-Exactly 'source/daemon/src/server.cpp' @'
        th.t = std::thread([hPipe, st, backend, read_only, require_auth, admin_logs, no_clipboard,
                            auth_keys_data, audit_all, done = th.done]() {
          handle_client(hPipe, st, backend, read_only, require_auth, admin_logs, no_clipboard,
                        auth_keys_data, audit_all);
          *done = true;
        });
        st->client_threads.push_back(std::move(th));
'@ @'
        th.t = std::thread([hPipe, st, backend, read_only, require_auth, admin_logs, no_clipboard,
                            auth_keys_data, audit_all, done = th.done]() {
          handle_client(hPipe, st, backend, read_only, require_auth, admin_logs, no_clipboard,
                        auth_keys_data, audit_all);
          *done = true;
        });
        HANDLE thread_handle = th.t.native_handle();
        th.cancel = [thread_handle]() {
          (void)CancelSynchronousIo(thread_handle);
        };
        st->client_threads.push_back(std::move(th));
'@

Replace-Exactly 'source/daemon/src/server.cpp' @'
  auto running = std::make_shared<std::atomic<bool>>(true);

  LOG_INFO("WinInspect Daemon " + std::string(wininspect::WININSPECT_VERSION) +
'@ @'
  auto running = std::make_shared<std::atomic<bool>>(true);
  st->running = running;

  LOG_INFO("WinInspect Daemon " + std::string(wininspect::WININSPECT_VERSION) +
'@

Replace-Exactly 'source/daemon/src/server.cpp' @'
      wininspect::CoreEngine http_core(backend);
      http_core.set_read_only(read_only);
      http_core.set_license_info(net_cfg.deployment, net_cfg.license_type);
'@ @'
      wininspect::CoreEngine http_core(backend);
      http_core.set_read_only(read_only);
      http_core.set_shutdown_hook([st]() { st->request_shutdown(); });
      http_core.set_license_info(net_cfg.deployment, net_cfg.license_type);
'@

Replace-Exactly 'source/daemon/src/server.cpp' @'
      bg_threads.emplace_back([running, backend = backend.get(), &net_cfg, read_only, no_clipboard,
                               http_token, cert_pem_str, key_pem_str]() {
        wininspect::CoreEngine https_core(backend);
        https_core.set_license_info(net_cfg.deployment, net_cfg.license_type);
'@ @'
      bg_threads.emplace_back([running, backend = backend.get(), &net_cfg, read_only, no_clipboard,
                               http_token, cert_pem_str, key_pem_str, st = st.get()]() {
        wininspect::CoreEngine https_core(backend);
        https_core.set_shutdown_hook([st]() { st->request_shutdown(); });
        https_core.set_license_info(net_cfg.deployment, net_cfg.license_type);
'@

Replace-Exactly 'source/daemon/src/server.cpp' @'
    wininspectd::TrayManager tray([running = running, st = st.get(), tcp = tcp]() {
      LOG_INFO("Shutdown requested via tray.");
      *running = false;
      tcp->stop();
    });
'@ @'
    wininspectd::TrayManager tray([running = running, st = st.get(), tcp = tcp]() {
      LOG_INFO("Shutdown requested via tray.");
      st->request_shutdown();
      st->activate_shutdown();
      tcp->stop();
    });
'@

Replace-Exactly 'source/daemon/src/server.cpp' @'
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
'@ @'
  // Signal shutdown and wait for background threads to finish.
  st->request_shutdown();
  st->activate_shutdown();
  // Persist audit log to disk
  st->control->save_audit_log(audit_log_path);
  LOG_INFO("Shutting down background threads...");
  for (auto& t : bg_threads) {
    if (t.joinable())
      t.join();
  }

  // No listener can create another client after background threads have joined.
  // Move the remaining client set out under lock, cancel blocked synchronous I/O,
  // then join every owned thread before ServerState destruction.
  std::list<ThreadHandle> clients;
  {
    std::lock_guard<std::mutex> lk(st->thread_mu);
    clients.splice(clients.end(), st->client_threads);
  }
  for (auto& th : clients) {
    if (!th.done->load() && th.cancel)
      th.cancel();
  }
  for (auto& th : clients) {
    if (th.t.joinable())
      th.t.join();
  }

  {
    std::lock_guard<std::mutex> lk(st->daemon_state_mu);
    if (st->daemon_state != ServerState::State::Failed)
      st->daemon_state = ServerState::State::Stopped;
  }
  LOG_INFO("Daemon shutdown complete.");
  return 0;
'@

# TCP/TLS follows the same acknowledge-then-activate contract. Socket shutdown is
# used only to wake other blocked client handlers during final drain.
Replace-Exactly 'source/daemon/src/tcp_server.cpp' @'
    wininspect::CoreEngine core(backend);
    core.set_admin_logs_enabled(admin_logs);
    core.set_read_only(read_only);
    if (audit_all && st->control) {
'@ @'
    wininspect::CoreEngine core(backend);
    core.set_admin_logs_enabled(admin_logs);
    core.set_read_only(read_only);
    core.set_shutdown_hook([st]() { st->request_shutdown(); });
    if (audit_all && st->control) {
'@

Replace-Exactly 'source/daemon/src/tcp_server.cpp' @'
      if (was_compressed) {
        // prepare_response already packed compressed framing
        if (!socket_write_all(s, raw_out.data(), (uint32_t)raw_out.size(), tls))
          break;
      }
      else if (encrypted) {
        if (!encrypted_send(s, raw_out, crypto))
          break;
      }
      else {
        uint32_t out_len = (uint32_t)raw_out.size();
        uint32_t out_len_net = htonl(out_len);
        if (!socket_write_all(s, &out_len_net, 4, tls) ||
            !socket_write_all(s, raw_out.data(), out_len, tls))
          break;
      }
      // PinGuard RAII handles unpin automatically at end of iteration
'@ @'
      bool write_ok = true;
      if (was_compressed) {
        // prepare_response already packed compressed framing
        if (!socket_write_all(s, raw_out.data(), (uint32_t)raw_out.size(), tls))
          write_ok = false;
      }
      else if (encrypted) {
        if (!encrypted_send(s, raw_out, crypto))
          write_ok = false;
      }
      else {
        uint32_t out_len = (uint32_t)raw_out.size();
        uint32_t out_len_net = htonl(out_len);
        if (!socket_write_all(s, &out_len_net, 4, tls) ||
            !socket_write_all(s, raw_out.data(), out_len, tls))
          write_ok = false;
      }
      bool shutdown_after_response = st->shutdown_pending();
      if (shutdown_after_response)
        st->activate_shutdown();
      if (!write_ok || shutdown_after_response)
        break;
      // PinGuard RAII handles unpin automatically at end of iteration
'@

# Both TLS and ordinary TCP thread creation sites have the same push line. Patch
# them independently by replacing the exact larger blocks.
Replace-Exactly 'source/daemon/src/tcp_server.cpp' @'
        th.t = std::thread(
            [this, client, auth_keys, read_only, admin_logs, no_clipboard, tls, done = th.done]() {
              handle_socket_client(client, state_, backend_, auth_keys, read_only, admin_logs,
                                   no_clipboard, backend_->get_instance_identity(), 1800000, false,
                                   tls.get());
              *done = true;
            });
        state_->client_threads.push_back(std::move(th));
'@ @'
        th.t = std::thread(
            [this, client, auth_keys, read_only, admin_logs, no_clipboard, tls, done = th.done]() {
              handle_socket_client(client, state_, backend_, auth_keys, read_only, admin_logs,
                                   no_clipboard, backend_->get_instance_identity(), 1800000, false,
                                   tls.get());
              *done = true;
            });
        th.cancel = [client]() { (void)::shutdown(client, SD_BOTH); };
        state_->client_threads.push_back(std::move(th));
'@

Replace-Exactly 'source/daemon/src/tcp_server.cpp' @'
          th.t = std::thread([this, client, cfg, auth_keys, read_only, admin_logs, no_clipboard,
                              done = th.done]() {
            handle_socket_client(client, state_, backend_, auth_keys, read_only, admin_logs,
                                 no_clipboard, backend_->get_instance_identity(),
                                 cfg.tcp_idle_timeout_ms);
            *done = true;
          });
          state_->client_threads.push_back(std::move(th));
'@ @'
          th.t = std::thread([this, client, cfg, auth_keys, read_only, admin_logs, no_clipboard,
                              done = th.done]() {
            handle_socket_client(client, state_, backend_, auth_keys, read_only, admin_logs,
                                 no_clipboard, backend_->get_instance_identity(),
                                 cfg.tcp_idle_timeout_ms);
            *done = true;
          });
          th.cancel = [client]() { (void)::shutdown(client, SD_BOTH); };
          state_->client_threads.push_back(std::move(th));
'@

# Focused regression becomes part of the generated candidate checkout.
$testPath = 'source/tests/test_daemon_shutdown_lifecycle.cpp'
$test = @'
// SPDX-License-Identifier: PolyForm-NC-1.0.0
#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"
#include "server_state.hpp"
#include <cassert>
#include <memory>

int main()
{
  using namespace wininspect;

  FakeBackend backend;
  CoreEngine core(&backend);
  ServerState state;
  state.running = std::make_shared<std::atomic<bool>>(true);
  state.daemon_state = ServerState::State::Running;
  core.set_shutdown_hook([&state]() { state.request_shutdown(); });

  CoreRequest req;
  req.id = "shutdown-test";
  req.method = "daemon.shutdown";
  Snapshot snap = backend.capture_snapshot();

  auto response = core.handle(req, snap, nullptr);
  assert(response.ok);
  assert(state.shutdown_pending());
  assert(state.daemon_state == ServerState::State::Draining);
  assert(state.running->load());

  state.activate_shutdown();
  assert(!state.running->load());

  state.request_shutdown();
  assert(state.daemon_state == ServerState::State::Draining);
  state.daemon_state = ServerState::State::Stopped;
  state.request_shutdown();
  assert(state.daemon_state == ServerState::State::Stopped);

  CoreEngine detached_core(&backend);
  auto unavailable = detached_core.handle(req, snap, nullptr);
  assert(!unavailable.ok);
  assert(unavailable.error_code == "E_SHUTDOWN_UNAVAILABLE");
  return 0;
}
'@
[IO.File]::WriteAllText($testPath, $test.Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))

Replace-Exactly 'source/CMakeLists.txt' @'
  add_executable(test_discovery
    daemon/src/test_discovery.cpp
  )
'@ @'
  add_executable(test_daemon_shutdown_lifecycle
    tests/test_daemon_shutdown_lifecycle.cpp
  )
  target_include_directories(test_daemon_shutdown_lifecycle PRIVATE
    core/include third_party daemon/src daemon/include)
  target_link_libraries(test_daemon_shutdown_lifecycle PRIVATE wininspect_core)
  add_test(NAME test_daemon_shutdown_lifecycle COMMAND test_daemon_shutdown_lifecycle WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  add_executable(test_discovery
    daemon/src/test_discovery.cpp
  )
'@

Write-Host 'V2-B1 candidate transform applied successfully.'
