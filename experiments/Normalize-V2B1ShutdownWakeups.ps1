$ErrorActionPreference = 'Stop'

function Replace-Exactly {
    param([string]$Path,[string]$Old,[string]$New)
    $text = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $oldNorm = $Old.Replace("`r`n", "`n")
    $newNorm = $New.Replace("`r`n", "`n")
    $first = $text.IndexOf($oldNorm,[StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Expected shutdown-wakeup block not found in $Path" }
    if ($text.IndexOf($oldNorm,$first + $oldNorm.Length,[StringComparison]::Ordinal) -ge 0) { throw "Shutdown-wakeup block is not unique in $Path" }
    $updated = $text.Substring(0,$first) + $newNorm + $text.Substring($first + $oldNorm.Length)
    [IO.File]::WriteAllText($Path,$updated,[Text.UTF8Encoding]::new($false))
}

$path = 'source/daemon/src/server.cpp'

Replace-Exactly $path @'
  std::wstring g_pipe_name = L"\\\\.\\pipe\\wininspectd";

  void cleanup_sessions(ServerState* st)
'@ @'
  std::wstring g_pipe_name = L"\\\\.\\pipe\\wininspectd";

  bool wait_while_running(std::atomic<bool>* running, DWORD total_ms)
  {
    DWORD waited = 0;
    while (running->load() && waited < total_ms) {
      DWORD remaining = total_ms - waited;
      DWORD slice = remaining < 100 ? remaining : 100;
      Sleep(slice);
      waited += slice;
    }
    return running->load();
  }

  void wake_named_pipe_listener()
  {
    HANDLE wake = CreateFileW(g_pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE)
      CloseHandle(wake);
  }

  void cleanup_sessions(ServerState* st)
'@

Replace-Exactly $path @'
    while (running->load()) {
      Sleep(net_cfg.cleanup_interval_ms);
      cleanup_sessions(st);
      *health_ok = running->load();
    }
'@ @'
    while (running->load()) {
      if (!wait_while_running(running.get(), (DWORD)net_cfg.cleanup_interval_ms))
        break;
      cleanup_sessions(st);
      *health_ok = running->load();
    }
'@

Replace-Exactly $path @'
            Sleep(net_cfg.update_check_interval_hours * 3600 * 1000);
'@ @'
            if (!wait_while_running(running.get(),
                                    (DWORD)(net_cfg.update_check_interval_hours * 3600 * 1000)))
              break;
'@

Replace-Exactly $path @'
            Sleep(10000);
          }
          else {
            heartbeat_fails = 0;
            Sleep(30000);
          }
'@ @'
            if (!wait_while_running(running.get(), 10000))
              break;
          }
          else {
            heartbeat_fails = 0;
            if (!wait_while_running(running.get(), 30000))
              break;
          }
'@

Replace-Exactly $path @'
      LOG_DEBUG("Named Pipe connection accepted.");

      {
'@ @'
      if (!running->load()) {
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
        break;
      }

      LOG_DEBUG("Named Pipe connection accepted.");

      {
'@

Replace-Exactly $path @'
  st->request_shutdown();
  st->activate_shutdown();
  // Persist audit log to disk
'@ @'
  st->request_shutdown();
  st->activate_shutdown();
  // Connect once to release a listener that may have entered blocking ConnectNamedPipe
  // in the short interval before the request handler cleared the shared run gate.
  wake_named_pipe_listener();
  // Persist audit log to disk
'@

Write-Host 'V2-B1 shutdown wakeups normalized: periodic workers are interruptible and the named-pipe listener is released before joins.'
