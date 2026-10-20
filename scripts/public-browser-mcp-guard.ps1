#Requires -Version 7.0
# Synthetic public-only Windows MCP contender; strictly one read-only tool, no egress.
[CmdletBinding()]
param([ValidateSet('Prepare','Cleanup')][string]$Mode='Prepare')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$stateFile=Join-Path $env:RUNNER_TEMP 'mcp-rdte-state.json'
if($Mode -eq 'Cleanup'){
  if(-not (Test-Path -LiteralPath $stateFile)){Write-Host 'MCP_CLEANUP=NO_STATE';exit 0}
  $s=Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
  $rule=Get-NetFirewallRule -Name $s.rule -ErrorAction SilentlyContinue
  if($rule){$rule | Remove-NetFirewallRule -ErrorAction Stop}
  $deleted=$false
  for($i=0;$i -lt 12;$i++){
    try{
      if(Test-Path -LiteralPath $s.work){Remove-Item -LiteralPath $s.work -Recurse -Force -ErrorAction Stop}
      $deleted=-not (Test-Path -LiteralPath $s.work)
      if($deleted){break}
    }catch{}
    Start-Sleep -Milliseconds 350
  }
  $ruleAbsent=-not (Get-NetFirewallRule -Name $s.rule -ErrorAction SilentlyContinue)
  Write-Host "MCP_CLEANUP=$($deleted -and $ruleAbsent)"
  if(-not $deleted -or -not $ruleAbsent){throw 'MCP guard cleanup failure'}
  Remove-Item -LiteralPath $stateFile -Force
  exit 0
}
$work=Join-Path $env:RUNNER_TEMP ('mcp-rdte-'+[Guid]::NewGuid().ToString('N'))
$ruleName='WININSPECT-MCP-RDTE-'+[Guid]::NewGuid().ToString('N')
[ordered]@{work=$work;rule=$ruleName;ready=$false} |
 ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8NoBOM
New-Item -ItemType Directory -Force -Path $work | Out-Null
$tag='v1.3.28'
$assetName='windows-mcp-server-1.3.28-win-x64.zip'
$headers=@{'User-Agent'='WinInspect-public-RDTE';'Accept'='application/vnd.github+json'}
$release=Invoke-RestMethod -Uri ('https://api.github.com/repos/sbroenne/mcp-windows/releases/tags/'+$tag) -Headers $headers -TimeoutSec 20
$asset=@($release.assets | Where-Object {$_.name -eq $assetName}) | Select-Object -First 1
if(-not $asset -or -not ([string]$asset.digest -match '^sha256:[0-9a-fA-F]{64}$')){throw 'No release digest'}
if($asset.size -gt 100MB){throw 'Archive too large'}
$zip=Join-Path $work $assetName
Invoke-WebRequest -Uri ([string]$asset.browser_download_url) -OutFile $zip -MaximumRedirection 5 -TimeoutSec 180
$observed=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
if($observed -ine ([string]$asset.digest).Substring(7)){throw 'Pinned MCP archive SHA mismatch'}
$root=Join-Path $work 'server'
Expand-Archive -LiteralPath $zip -DestinationPath $root -Force
Remove-Item -LiteralPath $zip -Force
$bins=@(Get-ChildItem -LiteralPath $root -Recurse -File -Filter Sbroenne.WindowsMcp.exe)
if($bins.Count -ne 1){throw 'Exactly one server executable required'}
$exe=$bins[0].FullName
$allExes=@(Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.exe')
if($allExes.Count -ne 1){throw 'Unexpected executable in pinned artifact'}
New-NetFirewallRule -Name $ruleName -DisplayName $ruleName -Direction Outbound -Action Block -Enabled True -Profile Any -Program $exe -ErrorAction Stop | Out-Null
$fw=Get-NetFirewallRule -Name $ruleName -ErrorAction Stop
$app=$fw | Get-NetFirewallApplicationFilter -ErrorAction Stop
if($fw.Action -ne 'Block' -or $fw.Direction -ne 'Outbound' -or $fw.Enabled -ne 'True' -or $app.Program -ne $exe){
 throw 'Verified executable-specific network block missing'
}
Add-Content -LiteralPath $env:GITHUB_ENV -Value "MCP_RDTE_EXE=$exe" -Encoding utf8NoBOM
Add-Content -LiteralPath $env:GITHUB_ENV -Value 'MCP_RDTE_EGRESS_DENIED=1' -Encoding utf8NoBOM
Add-Content -LiteralPath $env:GITHUB_ENV -Value 'WINDOWS_MCP_TOOLS=ui_snapshot' -Encoding utf8NoBOM
$tail=@(Get-Content -LiteralPath $env:GITHUB_ENV -Tail 3)
if($tail.Count -ne 3 -or $tail[2] -ne 'WINDOWS_MCP_TOOLS=ui_snapshot'){throw 'MCP policy env export invalid'}
[ordered]@{work=$work;rule=$ruleName;ready=$true} |
 ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8NoBOM
Write-Host 'MCP_RDTE_VENDOR_PROVENANCE=SHA256_PASS ALLOWLIST=ui_snapshot EGRESS=BLOCKED'
