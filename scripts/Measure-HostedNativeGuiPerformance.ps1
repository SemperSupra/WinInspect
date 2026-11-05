param(
  [Parameter(Mandatory=$true)][string]$GuiPath,
  [string]$OutputPath = 'evidence/gui-performance.json',
  [int]$Repetitions = 5
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $GuiPath)) { throw "GUI executable not found: $GuiPath" }
if ($Repetitions -lt 3) { throw 'Use at least three repetitions for characterization.' }

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WinInspectPerfNative {
  [DllImport("user32.dll", SetLastError=true)]
  public static extern IntPtr SendMessageTimeout(
    IntPtr hWnd, uint Msg, UIntPtr wParam, IntPtr lParam,
    uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);
}
'@

$SMTO_ABORTIFHUNG = 0x0002
$WM_NULL = 0x0000
$reps = @()

for ($i = 1; $i -le $Repetitions; $i++) {
  $launchUtc = [DateTime]::UtcNow
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $p = Start-Process -FilePath $GuiPath -PassThru
  try {
    $hwnd = [IntPtr]::Zero
    $hwndMs = $null
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
      $p.Refresh()
      if ($p.HasExited) { throw "GUI exited before exposing a main window on repetition $i." }
      if ($p.MainWindowHandle -ne 0) {
        $hwnd = [IntPtr]$p.MainWindowHandle
        $hwndMs = [math]::Round($sw.Elapsed.TotalMilliseconds, 3)
        break
      }
      Start-Sleep -Milliseconds 20
    }
    if ($hwnd -eq [IntPtr]::Zero) { throw "Timed out waiting for GUI HWND on repetition $i." }

    $root = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
    $connect = $null
    $uiaDeadline = [DateTime]::UtcNow.AddSeconds(10)
    $nameCondition = New-Object System.Windows.Automation.PropertyCondition(
      [System.Windows.Automation.AutomationElement]::NameProperty, 'Connect')
    while ([DateTime]::UtcNow -lt $uiaDeadline) {
      $connect = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $nameCondition)
      if ($connect) { break }
      Start-Sleep -Milliseconds 20
    }
    if (-not $connect) { throw "Primary Connect affordance not UIA-ready on repetition $i." }
    $uiaMs = [math]::Round($sw.Elapsed.TotalMilliseconds, 3)

    # Sample a cheap UI-thread health sensor. This is characterization, not a
    # universal UX threshold: record the elapsed time and only fail on an actual
    # timeout/hang.
    $latencies = @()
    foreach ($sample in 1..5) {
      $probe = [Diagnostics.Stopwatch]::StartNew()
      [UIntPtr]$ignored = [UIntPtr]::Zero
      $ret = [WinInspectPerfNative]::SendMessageTimeout(
        $hwnd, $WM_NULL, [UIntPtr]::Zero, [IntPtr]::Zero,
        $SMTO_ABORTIFHUNG, 2000, [ref]$ignored)
      $probe.Stop()
      if ($ret -eq [IntPtr]::Zero) { throw "WM_NULL responsiveness probe timed out on repetition $i." }
      $latencies += [math]::Round($probe.Elapsed.TotalMilliseconds, 3)
      Start-Sleep -Milliseconds 20
    }

    Start-Sleep -Milliseconds 250
    $p.Refresh()
    $cpu0 = $p.TotalProcessorTime.TotalMilliseconds
    $workingSet = $p.WorkingSet64
    Start-Sleep -Milliseconds 250
    $p.Refresh()
    $cpu1 = $p.TotalProcessorTime.TotalMilliseconds

    $sortedLatency = @($latencies | Sort-Object)
    $medianLatency = $sortedLatency[[int][math]::Floor($sortedLatency.Count / 2)]
    $reps += [ordered]@{
      repetition = $i
      cache_phase = $(if ($i -eq 1) { 'first-launch' } else { 'subsequent-launch' })
      launch_utc = $launchUtc.ToString('o')
      hwnd_ready_ms = $hwndMs
      primary_uia_ready_ms = $uiaMs
      wm_null_ms = $latencies
      wm_null_median_ms = $medianLatency
      working_set_bytes = [int64]$workingSet
      cpu_ms_over_250ms_sample = [math]::Round(($cpu1 - $cpu0), 3)
    }
  }
  finally {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    if ($p) { $p.Dispose() }
  }
}

function Median([object[]]$values) {
  $s = @($values | Sort-Object)
  if ($s.Count -eq 0) { return $null }
  $n = $s.Count
  if (($n % 2) -eq 1) { return $s[[int][math]::Floor($n / 2)] }
  return [math]::Round((([double]$s[$n/2-1] + [double]$s[$n/2]) / 2.0), 3)
}

$summary = [ordered]@{
  schema = 'wininspect.hosted-native-gui-performance.v1'
  timestamp_utc = [DateTime]::UtcNow.ToString('o')
  repetitions = $Repetitions
  interpretation = 'characterization-only; no universal UX threshold applied'
  first_launch_hwnd_ms = $reps[0].hwnd_ready_ms
  first_launch_uia_ms = $reps[0].primary_uia_ready_ms
  subsequent_hwnd_median_ms = Median @($reps | Select-Object -Skip 1 | ForEach-Object { $_.hwnd_ready_ms })
  subsequent_uia_median_ms = Median @($reps | Select-Object -Skip 1 | ForEach-Object { $_.primary_uia_ready_ms })
  wm_null_median_of_medians_ms = Median @($reps | ForEach-Object { $_.wm_null_median_ms })
  working_set_median_bytes = Median @($reps | ForEach-Object { $_.working_set_bytes })
  cpu_sample_median_ms = Median @($reps | ForEach-Object { $_.cpu_ms_over_250ms_sample })
  samples = $reps
}

$dir = Split-Path -Parent $OutputPath
if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
$summary | ConvertTo-Json -Depth 8 | Set-Content $OutputPath -Encoding utf8
$summary | ConvertTo-Json -Depth 8
