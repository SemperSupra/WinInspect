#Requires -Version 7.0
# Disposable runner only: verify pinned WinApp release, deny vendor executable egress, expose read-only binary.
[CmdletBinding()]
param([ValidateSet('Prepare','Cleanup')][string]$Mode='Prepare')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$stateFile=Join-Path $env:RUNNER_TEMP 'winapp-rdte-guard-state.json'
if($Mode -eq 'Cleanup'){
  if(-not (Test-Path -LiteralPath $stateFile)){Write-Host 'WINAPP_CLEANUP=NO_STATE';exit 0}
  $s=Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
  if($s.rule){
    $rule=Get-NetFirewallRule -Name $s.rule -ErrorAction SilentlyContinue
    if($rule){$rule | Remove-NetFirewallRule -ErrorAction Stop}
  }
  $deleted=$false
  for($i=0;$i -lt 8;$i++){
    try {
      if(Test-Path -LiteralPath $s.work){Remove-Item -LiteralPath $s.work -Recurse -Force -ErrorAction Stop}
      $deleted=-not (Test-Path -LiteralPath $s.work)
      if($deleted){break}
    }catch{}
    Start-Sleep -Milliseconds 350
  }
  $rulesGone= -not (Get-NetFirewallRule -Name $s.rule -ErrorAction SilentlyContinue)
  Write-Host "WINAPP_CLEANUP=$($deleted -and $rulesGone)"
  if(-not $deleted -or -not $rulesGone){throw 'Vendor guard cleanup failed'}
  Remove-Item -LiteralPath $stateFile -Force
  exit 0
}
$work=Join-Path $env:RUNNER_TEMP ('winapp-rdte-'+[Guid]::NewGuid().ToString('N'))
$ruleName='WININSPECT-RDTE-'+[Guid]::NewGuid().ToString('N')
$s=[ordered]@{work=$work;rule=$ruleName;ready=$false}
$s | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8NoBOM
New-Item -ItemType Directory -Force -Path $work | Out-Null
$tag='v0.7.1';$assetName='winappcli-x64.zip'
# Frozen upstream release asset digest and length were independently checked
# against GitHub's official v0.7.1 release metadata before this change.
# An anonymous REST metadata lookup on each disposable runner exhausted GitHub's
# shared unauthenticated rate limit (403), despite the asset itself being pinned.
# Never trust a mutable asset by URL alone: fail closed on exact size + SHA-256.
$expectedSha256='d925d1e32cdc320b6d271f653fd2cd3c05b5d7558deb20d1b548bead77900fcf'
$expectedBytes=[int64]99013508
if($expectedBytes -gt 120MB){throw 'Pinned archive exceeds size limit'}
$downloadUrl='https://github.com/microsoft/winappCli/releases/download/'+$tag+'/'+$assetName
$zip=Join-Path $work $assetName
Invoke-WebRequest -Uri $downloadUrl -OutFile $zip -MaximumRedirection 5 -TimeoutSec 180
$actualBytes=(Get-Item -LiteralPath $zip -ErrorAction Stop).Length
if($actualBytes -ne $expectedBytes){throw 'Pinned archive length mismatch'}
$hash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
if($hash -ine $expectedSha256){throw 'Pinned archive hash mismatch'}
$root=Join-Path $work 'bin'
Expand-Archive -LiteralPath $zip -DestinationPath $root -Force
Remove-Item -LiteralPath $zip -Force
$bins=@(Get-ChildItem -LiteralPath $root -Recurse -File -Filter winapp.exe)
if($bins.Count -ne 1){throw 'Expected one winapp.exe'}
$exe=$bins[0].FullName
$allExecutables=@(Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.exe')
if($allExecutables.Count -ne 1){throw 'Unexpected additional executable - refuse auto-execution'}
New-NetFirewallRule -Name $ruleName -DisplayName $ruleName -Direction Outbound -Action Block -Profile Any -Enabled True -Program $exe -ErrorAction Stop | Out-Null
$fw=Get-NetFirewallRule -Name $ruleName -ErrorAction Stop
$app=$fw | Get-NetFirewallApplicationFilter -ErrorAction Stop
if($fw.Action -ne 'Block' -or $fw.Direction -ne 'Outbound' -or $fw.Enabled -ne 'True' -or $app.Program -ne $exe){
 throw 'Outbound deny rule not verified'
}
$cache=Join-Path $work 'cache'
New-Item -ItemType Directory -Force -Path $cache | Out-Null
Add-Content -LiteralPath $env:GITHUB_ENV -Value "WINAPP_EXE=$exe" -Encoding utf8NoBOM
Add-Content -LiteralPath $env:GITHUB_ENV -Value "WINAPP_CLI_CACHE_DIRECTORY=$cache" -Encoding utf8NoBOM
Add-Content -LiteralPath $env:GITHUB_ENV -Value 'WINAPP_CLI_TELEMETRY_OPTOUT=1' -Encoding utf8NoBOM
Add-Content -LiteralPath $env:GITHUB_ENV -Value 'WINAPP_CLI_UPDATE_CHECK=0' -Encoding utf8NoBOM
Add-Content -LiteralPath $env:GITHUB_ENV -Value 'WINAPP_EGRESS_DENIED=1' -Encoding utf8NoBOM
$tail=@(Get-Content -LiteralPath $env:GITHUB_ENV -Tail 5)
if($tail.Count -ne 5 -or $tail[0] -notmatch '^WINAPP_EXE=' -or $tail[4] -ne 'WINAPP_EGRESS_DENIED=1'){
 throw 'Runner environment boundary does not contain five distinct entries'
}
$s.ready=$true
$s | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8NoBOM
Write-Host 'WINAPP_VENDOR_PROVENANCE=SHA256_PASS EGRESS=BLOCKED TELEMETRY=OPT_OUT'
