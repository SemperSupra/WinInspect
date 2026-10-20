#Requires -Version 7.0
# Public-safe execution smoke. No private sources/tests, network requests,
# persistent server, credentials, application automation or release actions.
[CmdletBinding()]
param(
    [string] $RepositoryRoot = (Resolve-Path '.').Path,
    [string] $BuildRoot = 'build',
    [int] $TimeoutSeconds = 20
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$built = Join-Path $root $BuildRoot
if (-not (Test-Path -LiteralPath $built -PathType Container)) {
    throw 'Projected source build output is absent.'
}
$daemonCandidates = @(Get-ChildItem -LiteralPath $built -Recurse -File -Filter 'wininspectd.exe')
if ($daemonCandidates.Count -ne 1) {
    throw "Expected exactly one projected wininspectd.exe, got $($daemonCandidates.Count)."
}
$daemon = $daemonCandidates[0].FullName
$portable = Join-Path $built 'wi-portable-projected-win-x64.exe'
if (-not (Test-Path -LiteralPath $portable -PathType Leaf)) {
    throw 'Projected portable client binary missing.'
}

$testData = Join-Path ([IO.Path]::GetTempPath()) ('wininspect-public-smoke-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $testData | Out-Null
try {
    function Invoke-IsolatedBinary {
        param([string] $Executable, [string[]] $Argv)
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = $Executable
        $info.WorkingDirectory = $testData
        $info.UseShellExecute = $false
        $info.RedirectStandardOutput = $true
        $info.RedirectStandardError = $true
        $info.CreateNoWindow = $true
        $info.Environment['APPDATA'] = $testData
        $info.Environment['WININSPECT_BIND'] = '127.0.0.1'
        foreach ($arg in $Argv) { [void]$info.ArgumentList.Add($arg) }
        $proc = [Diagnostics.Process]::new()
        $proc.StartInfo = $info
        try {
            if (-not $proc.Start()) { throw 'Failed to launch projected executable.' }
            if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
                $proc.Kill($true)
                throw 'Projected executable timed out.'
            }
            $out = $proc.StandardOutput.ReadToEnd()
            $err = $proc.StandardError.ReadToEnd()
            if ($proc.ExitCode -ne 0) {
                throw "Projected executable returned exit $($proc.ExitCode) (stderr bytes: $($err.Length))."
            }
            return $out
        } finally {
            if (-not $proc.HasExited) {
                try { $proc.Kill($true) } catch { }
            }
            $proc.Dispose()
        }
    }

    $configText = Invoke-IsolatedBinary -Executable $daemon -Argv @(
        '--no-config', '--dump-config', '--bind', '127.0.0.1',
        '--ipv4', '--no-discovery', '--no-mdns'
    )
    # Only the final JSON object is relevant; do not print machine identity or config.
    $configLine = @($configText -split '[\r\n]+' | Where-Object { $_.TrimStart().StartsWith('{') } | Select-Object -Last 1)
    if ($configLine.Count -ne 1) { throw 'Daemon did not emit a JSON configuration document.' }
    $cfg = $configLine[0] | ConvertFrom-Json -ErrorAction Stop
    if ($null -eq $cfg.bind) { throw 'Daemon config has no bind list.' }
    $bindText = $cfg.bind | ConvertTo-Json -Depth 5 -Compress
    if (-not $bindText.Contains('127.0.0.1') -or $bindText.Contains('0.0.0.0')) {
        throw 'Daemon config failed loopback-only binding smoke.'
    }
    $usageText = Invoke-IsolatedBinary -Executable $portable -Argv @()
    if (-not $usageText.Contains('Usage: wi-portable')) {
        throw 'Projected portable client did not emit the expected usage.'
    }
    Write-Host 'PASS: compiled projected daemon executed --dump-config with loopback-only bind.'
    Write-Host 'PASS: compiled projected portable CLI emitted its usage.'
    Write-Host "Daemon binary SHA-256: $((Get-FileHash -LiteralPath $daemon -Algorithm SHA256).Hash.ToLowerInvariant())"
    Write-Host "Portable binary SHA-256: $((Get-FileHash -LiteralPath $portable -Algorithm SHA256).Hash.ToLowerInvariant())"
    Write-Host 'Scope: configuration/CLI binary smoke only, NOT live RPC, native UIA, WinBot or v0.5 qualification.'
} finally {
    if (Test-Path -LiteralPath $testData) {
        Remove-Item -LiteralPath $testData -Recurse -Force -ErrorAction SilentlyContinue
    }
}
