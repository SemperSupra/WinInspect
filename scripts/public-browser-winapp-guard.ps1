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
$api='https://api.github.com/repos/microsoft/winappCli/releases/tags/'+$tag
$headers=@{'User-Agent'='WinInspect-RDTE';'Accept'='application/vnd.github+json'}
$r=Invoke-RestMethod -Uri $api -Headers $headers -TimeoutSec 20
$asset=@($r.assets | Where-Object {$_.name -eq $assetName}) | Select-Object -First 1
if(-not $asset -or -not ([string]$asset.digest -match '^sha256:[0-9a-fA-F]{64}$')){throw 'No trusted release digest'}
if($asset.size -gt 120MB){throw 'Archive exceeds size limit'}
$zip=Join-Path $work $assetName
Invoke-WebRequest -Uri ([string]$asset.browser_download_url) -OutFile $zip -MaximumRedirection 5 -TimeoutSec 180
$hash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
if($hash -ine ([string]$asset.digest).Substring(7)){throw 'Archive hash mismatch'}
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
$envEntries=@(
  'WINAPP_EXE='+$exe,
  'WINAPP_CLI_CACHE_DIRECTORY='+$cache,
  'WINAPP_CLI_TELEMETRY_OPTOUT=1',
  'WINAPP_CLI_UPDATE_CHECK=0',
  'WINAPP_EGRESS_DENIED=1'
)
foreach($line in $envEntries){ Add-Content -LiteralPath $env:GITHUB_ENV -Value $line -Encoding utf8NoBOM }
$s.ready=$true
$s | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8NoBOM
Write-Host 'WINAPP_VENDOR_PROVENANCE=SHA256_PASS EGRESS=BLOCKED TELEMETRY=OPT_OUT'
