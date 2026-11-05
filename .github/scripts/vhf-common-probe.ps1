param(
  [Parameter(Mandatory=$true)][ValidateSet('Build','Observe')][string]$Mode,
  [string]$CommonProbeSha = $env:COMMON_PROBE_SHA,
  [string]$EvidenceDir = 'evidence'
)

$ErrorActionPreference = 'Stop'
$EvidenceDir = (New-Item -ItemType Directory -Force -Path $EvidenceDir).FullName

function Get-LineCount([string]$Path) {
  if (-not (Test-Path $Path)) { return 0 }
  return @(Get-Content $Path).Count
}

function Write-Slice([string]$Source, [int]$Start, [int]$End, [string]$Destination) {
  $lines = @(Get-Content $Source)
  if ($End -gt $Start -and $Start -lt $lines.Count) {
    $last = [Math]::Min($End - 1, $lines.Count - 1)
    @($lines[$Start..$last]) | Set-Content $Destination -Encoding utf8
  } else {
    New-Item -ItemType File -Force -Path $Destination | Out-Null
  }
}

if ($Mode -eq 'Build') {
  if (-not $CommonProbeSha) { throw 'COMMON_PROBE_SHA is required' }
  git fetch --no-tags --depth=1 origin $CommonProbeSha
  if ($LASTEXITCODE -ne 0) { throw "common-probe fetch failed: $LASTEXITCODE" }

  git show "$($CommonProbeSha):experiments/wine-uinput/wine_input_probe.cpp" |
    Set-Content (Join-Path $EvidenceDir 'win32_input_probe.cpp') -Encoding utf8NoBOM
  if ($LASTEXITCODE -ne 0) { throw "probe source extraction failed: $LASTEXITCODE" }
  git show "$($CommonProbeSha):experiments/wine-uinput/sendinput_control.cpp" |
    Set-Content (Join-Path $EvidenceDir 'sendinput_control.cpp') -Encoding utf8NoBOM
  if ($LASTEXITCODE -ne 0) { throw "SendInput source extraction failed: $LASTEXITCODE" }

  $vs = 'C:\Program Files\Microsoft Visual Studio\18\Enterprise'
  Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
  Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'

  & cl.exe /nologo /std:c++17 /EHsc /O2 /MT /W4 /WX /Fe:"$EvidenceDir\win32_input_probe.exe" "$EvidenceDir\win32_input_probe.cpp" user32.lib |
    Set-Content (Join-Path $EvidenceDir 'probe-build.txt')
  if ($LASTEXITCODE -ne 0) { throw "common probe compile failed: $LASTEXITCODE" }
  & cl.exe /nologo /std:c++17 /EHsc /O2 /MT /W4 /WX /Fe:"$EvidenceDir\sendinput_control.exe" "$EvidenceDir\sendinput_control.cpp" user32.lib |
    Set-Content (Join-Path $EvidenceDir 'sendinput-build.txt')
  if ($LASTEXITCODE -ne 0) { throw "SendInput control compile failed: $LASTEXITCODE" }

  Get-FileHash (Join-Path $EvidenceDir 'win32_input_probe.exe'),(Join-Path $EvidenceDir 'sendinput_control.exe') -Algorithm SHA256 |
    Select-Object Path,Hash | ConvertTo-Json -Depth 4 |
    Set-Content (Join-Path $EvidenceDir 'common-probe-hashes.json') -Encoding utf8
  [ordered]@{
    source_commit = $CommonProbeSha
    probe_source_path = 'experiments/wine-uinput/wine_input_probe.cpp'
    control_source_path = 'experiments/wine-uinput/sendinput_control.cpp'
  } | ConvertTo-Json | Set-Content (Join-Path $EvidenceDir 'common-probe-source.json') -Encoding utf8
  exit 0
}

$result = [ordered]@{
  classification = 'not-attempted'
  idle_input_events = 0
  sendinput_exit_code = $null
  sendinput_input_events = 0
  sendinput_raw_events = 0
  sendinput_named_device_events = 0
  vhf_raw_events = 0
  vhf_application_events = 0
  vhf_raw_device_names = @()
  vhf_raw_device_handles = @()
}

$probeOut = Join-Path $EvidenceDir 'probe.jsonl'
$probeErr = Join-Path $EvidenceDir 'probe.stderr'
$probe = Start-Process -FilePath (Join-Path $EvidenceDir 'win32_input_probe.exe') -RedirectStandardOutput $probeOut -RedirectStandardError $probeErr -PassThru
try {
  $ready = $false
  for ($i = 0; $i -lt 40; $i++) {
    if ((Test-Path $probeOut) -and (Select-String -Path $probeOut -Pattern '"event":"probe_ready"' -Quiet)) { $ready = $true; break }
    if ($probe.HasExited) { break }
    Start-Sleep -Milliseconds 250
  }
  if (-not $ready) {
    $result.classification = 'native-probe-not-ready'
    $result | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $EvidenceDir 'input-result.json') -Encoding utf8
    exit 1
  }

  $idleStart = Get-LineCount $probeOut
  Start-Sleep -Seconds 2
  $idleEnd = Get-LineCount $probeOut
  Write-Slice $probeOut $idleStart $idleEnd (Join-Path $EvidenceDir 'phase-idle.jsonl')

  & (Join-Path $EvidenceDir 'sendinput_control.exe') > (Join-Path $EvidenceDir 'sendinput-control.txt') 2>&1
  $result.sendinput_exit_code = $LASTEXITCODE
  Start-Sleep -Seconds 1
  $sendEnd = Get-LineCount $probeOut
  Write-Slice $probeOut $idleEnd $sendEnd (Join-Path $EvidenceDir 'phase-sendinput.jsonl')

  $beforeVhf = $sendEnd
  $vhfObserved = $false
  for ($i = 0; $i -lt 120; $i++) {
    $tail = @(Get-Content $probeOut | Select-Object -Skip $beforeVhf)
    if ($tail -match 'HID_DEVICE_SYSTEM_VHF') { $vhfObserved = $true; break }
    if ($probe.HasExited) { break }
    Start-Sleep -Milliseconds 250
  }
  Start-Sleep -Milliseconds 500
  $vhfEnd = Get-LineCount $probeOut
  Write-Slice $probeOut $beforeVhf $vhfEnd (Join-Path $EvidenceDir 'phase-vhf.jsonl')

  $idleLines = @(Get-Content (Join-Path $EvidenceDir 'phase-idle.jsonl'))
  $sendLines = @(Get-Content (Join-Path $EvidenceDir 'phase-sendinput.jsonl'))
  $vhfLines = @(Get-Content (Join-Path $EvidenceDir 'phase-vhf.jsonl'))
  $result.idle_input_events = @($idleLines | Where-Object { $_ -match '"event":"(raw_input|key_message|edit_changed)"' }).Count
  $result.sendinput_input_events = @($sendLines | Where-Object { $_ -match '"event":"(raw_input|key_message|edit_changed)"' }).Count
  $sendRaw = @($sendLines | Where-Object { $_ -match '"event":"raw_input"' })
  $result.sendinput_raw_events = $sendRaw.Count
  $result.sendinput_named_device_events = @($sendRaw | Where-Object { $_ -notmatch '"hDevice":"0x0"' -or $_ -notmatch '"device_name":""' }).Count
  $vhfRaw = @($vhfLines | Where-Object { $_ -match '"event":"raw_input"' -and $_ -match 'HID_DEVICE_SYSTEM_VHF' })
  $result.vhf_raw_events = $vhfRaw.Count
  $result.vhf_application_events = @($vhfLines | Where-Object { $_ -match '"event":"(key_message|edit_changed)"' }).Count
  $result.vhf_raw_device_names = @($vhfRaw | ForEach-Object { if ($_ -match '"device_name":"([^"]*)"') { $Matches[1] } } | Sort-Object -Unique)
  $result.vhf_raw_device_handles = @($vhfRaw | ForEach-Object { if ($_ -match '"hDevice":"([^"]*)"') { $Matches[1] } } | Sort-Object -Unique)

  if ($result.idle_input_events -eq 0 -and $result.sendinput_exit_code -eq 0 -and $result.sendinput_input_events -gt 0 -and $vhfObserved -and $result.vhf_raw_events -gt 0 -and $result.vhf_application_events -gt 0) {
    $result.classification = 'vhf-report-observed-with-paired-controls'
  } elseif (-not $vhfObserved) {
    $result.classification = 'vhf-child-started-but-report-not-observed'
  } elseif ($result.sendinput_exit_code -ne 0 -or $result.sendinput_input_events -eq 0) {
    $result.classification = 'native-sendinput-control-invalid'
  } elseif ($result.idle_input_events -ne 0) {
    $result.classification = 'native-idle-control-contaminated'
  } else {
    $result.classification = 'native-vhf-input-inconclusive'
  }

  $result | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $EvidenceDir 'input-result.json') -Encoding utf8
  Get-Content (Join-Path $EvidenceDir 'input-result.json')
  if ($result.classification -ne 'vhf-report-observed-with-paired-controls') { exit 1 }
} finally {
  if ($probe -and -not $probe.HasExited) { Stop-Process -Id $probe.Id -Force -ErrorAction SilentlyContinue }
}