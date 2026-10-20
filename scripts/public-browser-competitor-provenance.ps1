#Requires -Version 7.0
# Provenance-only competitor census. Downloads are never extracted or executed.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$headers = @{ 'User-Agent'='WinInspect-public-RDTE'; 'Accept'='application/vnd.github+json'; 'X-GitHub-Api-Version'='2022-11-28' }
$items = @(
  @{ name='microsoft-winappcli'; repo='microsoft/winappCli'; tag='v0.7.1'; asset='winappcli-x64.zip' },
  @{ name='windows-mcp'; repo='sbroenne/mcp-windows'; tag='v1.3.28'; asset='windows-mcp-server-1.3.28-win-x64.zip' }
)
$results = [System.Collections.Generic.List[object]]::new()
foreach ($item in $items) {
  $row = [ordered]@{ name=$item.name; tag=$item.tag; asset=$item.asset; status='NOT_OBSERVED'; size=0; digestVerified=$false; publishedAt=$null; reason=$null }
  try {
    $uri = 'https://api.github.com/repos/' + $item.repo + '/releases/tags/' + $item.tag
    $release = Invoke-RestMethod -Method Get -Uri $uri -Headers $headers -TimeoutSec 20
    $asset = @($release.assets | Where-Object { $_.name -eq $item.asset }) | Select-Object -First 1
    if (-not $asset) { $row.status='ASSET_MISSING' }
    elseif ($asset.size -gt 300MB) { $row.status='TOO_LARGE' }
    elseif (-not ([string]$asset.digest -match '^sha256:[a-f0-9]{64}$')) { $row.status='NO_TRUSTED_DIGEST' }
    else {
      $row.size = [int64]$asset.size
      $row.publishedAt = [string]$release.published_at
      $file = Join-Path $env:RUNNER_TEMP ($item.name + '-' + $item.tag + '.zip')
      try {
        Invoke-WebRequest -Uri ([string]$asset.browser_download_url) -OutFile $file -MaximumRedirection 5 -TimeoutSec 180
        $observed = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        $expected = ([string]$asset.digest).Substring(7).ToLowerInvariant()
        $row.digestVerified = ($observed -eq $expected)
        $row.status = if($row.digestVerified){'ZIP_SHA256_PASS'}else{'SHA256_MISMATCH'}
      } finally { if(Test-Path -LiteralPath $file){Remove-Item -LiteralPath $file -Force} }
    }
  } catch { $row.status='NETWORK_OR_API_BLOCKED'; $row.reason=$_.Exception.GetType().Name }
  [void]$results.Add($row)
  Write-Host ('COMPETITOR=' + $row.name + ' STATUS=' + $row.status)
}
$receipt = [ordered]@{schema='public-windows-competitor-provenance/v1';executablesLaunched=$false;noProfiles=$true;providers=$results}
$receipt | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $env:RUNNER_TEMP 'public-browser-competitor-provenance.json') -Encoding utf8NoBOM
