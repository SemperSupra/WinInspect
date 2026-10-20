#Requires -Version 7.0
# Public-safe live IPC smoke for already-projected product source only.
# Disposable runner; loopback TCP and unique local pipe; read-only request.
[CmdletBinding()]
param(
    [string] $RepositoryRoot = (Resolve-Path '.').Path,
    [string] $BuildRoot = 'build'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$bin = @(Get-ChildItem -LiteralPath (Join-Path $root $BuildRoot) -File -Recurse -Filter 'wininspectd.exe')
if ($bin.Count -ne 1) { throw "Expected one projected daemon, got $($bin.Count)." }
$work = Join-Path ([IO.Path]::GetTempPath()) ('wi-public-pipe-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
$proc = $null
$pipe = $null
try {
    $pipeName = 'wi-public-smoke-' + [Guid]::NewGuid().ToString('N')
    $cfgPath = Join-Path $work 'readonly-config.json'
    # Restrict configuration to loopback, disable discovery/updates/HTTP.
    @{
        config_version = 1
        bind = @(@{ address = '127.0.0.1'; family = 'ipv4' })
        port = 0
        http_port = 0
        https_port = 0
        tls_port = 0
        rendezvous = @()
        enable_update_check = $false
        enable_mdns = $false
        enable_discovery = $false
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $cfgPath -Encoding utf8NoBOM

    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $bin[0].FullName
    $info.WorkingDirectory = $work
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.Environment['APPDATA'] = $work
    $info.Environment['WININSPECT_BIND'] = '127.0.0.1'
    foreach ($arg in @('--config', $cfgPath, '--headless', '--read-only',
                        '--no-clipboard', '--no-discovery', '--no-mdns',
                        '--bind', '127.0.0.1', '--ipv4',
                        '--port', '0', '--pipe-name', $pipeName)) {
        [void]$info.ArgumentList.Add($arg)
    }
    $proc = [Diagnostics.Process]::Start($info)
    if ($null -eq $proc) { throw 'Cannot launch projected daemon.' }

    # Only local named-pipe IPC; retry briefly while process initializes.
    $connected = $false
    for ($i = 0; $i -lt 25; $i++) {
        if ($proc.HasExited) { throw "Projected daemon exited before pipe readiness: $($proc.ExitCode)." }
        $pipe = [IO.Pipes.NamedPipeClientStream]::new(
            '.', $pipeName, [IO.Pipes.PipeDirection]::InOut,
            [IO.Pipes.PipeOptions]::Asynchronous)
        try {
            $pipe.Connect(300)
            $connected = $true
            break
        } catch [TimeoutException] {
            $pipe.Dispose()
            $pipe = $null
            Start-Sleep -Milliseconds 150
        }
    }
    if (-not $connected) { throw 'Projected daemon did not accept local named pipe within bounded readiness interval.' }
    $request = '{"id":"public-smoke-health","method":"daemon.health","params":{"canonical":true}}'
    $payload = [Text.Encoding]::UTF8.GetBytes($request)
    if ($payload.Length -gt 4096) { throw 'Public fixture exceeded request cap.' }
    $header = [BitConverter]::GetBytes([uint32]$payload.Length)
    if (-not [BitConverter]::IsLittleEndian) { [Array]::Reverse($header) }
    $writeBudget = [Threading.CancellationTokenSource]::new(5000)
    try {
        $pipe.WriteAsync($header, 0, 4, $writeBudget.Token).GetAwaiter().GetResult()
        $pipe.WriteAsync($payload, 0, $payload.Length, $writeBudget.Token).GetAwaiter().GetResult()
        $pipe.FlushAsync($writeBudget.Token).GetAwaiter().GetResult()
    } finally {
        $writeBudget.Dispose()
    }

    function Read-Exact {
        param([IO.Stream] $Stream, [int] $Length)
        $bytes = [byte[]]::new($Length)
        $offset = 0
        $readBudget = [Threading.CancellationTokenSource]::new(5000)
        try {
            while ($offset -lt $Length) {
                $count = $Stream.ReadAsync($bytes, $offset, $Length - $offset,
                                           $readBudget.Token).GetAwaiter().GetResult()
                if ($count -le 0) { throw 'Unexpected end of local named-pipe frame.' }
                $offset += $count
            }
        } finally {
            $readBudget.Dispose()
        }
        return ,$bytes
    }
    $replyHeader = Read-Exact -Stream $pipe -Length 4
    if (-not [BitConverter]::IsLittleEndian) { [Array]::Reverse($replyHeader) }
    $length = [BitConverter]::ToUInt32($replyHeader, 0)
    if ($length -lt 2 -or $length -gt 65536) { throw "Unsafe public response length $length." }
    $reply = [Text.Encoding]::UTF8.GetString((Read-Exact -Stream $pipe -Length ([int]$length))) | ConvertFrom-Json -ErrorAction Stop
    if ($reply.id -ne 'public-smoke-health' -or $reply.ok -ne $true) {
        throw 'Projected daemon read-only health RPC returned an unexpected response.'
    }
    Write-Host 'PASS: projected Windows daemon accepted a local framed named-pipe read-only health request.'
    Write-Host 'PASS: response ID correlated; successful typed result; no private source or real app content.'
    Write-Host 'Scope: approved public baseline only; not v0.5 native pipe bounds, UIA or licensed application acceptance.'
} finally {
    if ($null -ne $pipe) {
        try { $pipe.Dispose() } catch { }
    }
    if ($null -ne $proc) {
        try {
            if (-not $proc.HasExited) { $proc.Kill($true) }
            [void]$proc.WaitForExit(5000)
        } finally { $proc.Dispose() }
    }
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Force -Recurse -ErrorAction SilentlyContinue
    }
}
