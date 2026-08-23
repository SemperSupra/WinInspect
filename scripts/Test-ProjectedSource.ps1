#Requires -Version 7.0
[CmdletBinding()]
param(
    [string] $RepositoryRoot = (Resolve-Path '.').Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Normalize-RelativePath([string] $Path) {
    return $Path.Replace('\', '/').TrimStart([char[]]@('.', '/'))
}

$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$sourceRoot = Join-Path $root 'source'
$manifestPath = Join-Path $root '.projection/source-manifest.json'
$sourceExists = Test-Path -LiteralPath $sourceRoot -PathType Container
$manifestExists = Test-Path -LiteralPath $manifestPath -PathType Leaf

if (-not $sourceExists -and -not $manifestExists) {
    Write-Host 'Public WinInspect deployment shell has no source projection yet; bootstrap validation passed.'
    exit 0
}
if ($sourceExists -ne $manifestExists) {
    throw 'Projected source and .projection/source-manifest.json must appear atomically.'
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or $manifest.projectionPolicyVersion -ne 1) {
    throw 'Unsupported projected-source manifest version.'
}
if ($manifest.sourceAuthority -ne 'private-development') {
    throw 'Projected-source manifest has an unexpected source authority marker.'
}
if ([string]$manifest.sourceCommitSha -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Projected-source manifest is missing an immutable 40-character source commit SHA.'
}
if ($manifest.authoritativeCandidate -ne $true) {
    throw 'Public builds require a source projection generated from a clean authoritative private source tree.'
}
if ([string]$manifest.projectionDigestSha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'Projected-source manifest projection digest is malformed.'
}

$entries = @($manifest.files)
if ($entries.Count -eq 0 -or [int]$manifest.fileCount -ne $entries.Count) {
    throw 'Projected-source manifest fileCount is empty or inconsistent.'
}

$manifestPaths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$canonicalLines = [System.Collections.Generic.List[string]]::new()
foreach ($entry in ($entries | Sort-Object path)) {
    $path = Normalize-RelativePath ([string]$entry.path)
    if (-not $manifestPaths.Add($path)) { throw "Duplicate projected-source manifest path: $path" }
    if ($path.Contains('..', [StringComparison]::Ordinal) -or [IO.Path]::IsPathRooted($path)) {
        throw "Unsafe projected-source path: $path"
    }
    foreach ($forbiddenPrefix in @('.github/','formal/','prompts/','private/','.git/')) {
        if ($path.StartsWith($forbiddenPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Private/control-plane path is forbidden in projected source: $path"
        }
    }
    if ($path -in @('AGENTS.md','.github/copilot-instructions.md')) {
        throw "Private agent/control metadata is forbidden in projected source: $path"
    }
    if ([string]$entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw "Malformed SHA-256 for projected path: $path" }
    if ([int64]$entry.bytes -lt 0) { throw "Negative byte count for projected path: $path" }

    $full = Join-Path $sourceRoot ($path -replace '/', [IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "Manifest-listed projected file is absent: $path" }
    $item = Get-Item -LiteralPath $full
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse/symlink projected files are forbidden: $path" }
    if ([int64]$item.Length -ne [int64]$entry.bytes) { throw "Projected file byte count mismatch: $path" }
    $actualSha = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualSha -ne ([string]$entry.sha256).ToLowerInvariant()) { throw "Projected file SHA-256 mismatch: $path" }
    $exec = [bool]$entry.executable
    $canonicalLines.Add("$path`t$($entry.bytes)`t$($actualSha)`t$($exec.ToString().ToLowerInvariant())")
}

$actualPaths = @(
    Get-ChildItem -LiteralPath $sourceRoot -File -Recurse -Force |
        ForEach-Object { Normalize-RelativePath ([IO.Path]::GetRelativePath($sourceRoot, $_.FullName)) } |
        Sort-Object
)
foreach ($path in $actualPaths) {
    if (-not $manifestPaths.Contains($path)) { throw "Unmanifested file exists in projected source: $path" }
}
if ($actualPaths.Count -ne $manifestPaths.Count) {
    throw "Projected source file count differs from manifest: disk=$($actualPaths.Count) manifest=$($manifestPaths.Count)"
}

$canonicalText = (($canonicalLines | Sort-Object) -join "`n") + "`n"
$digestBytes = [Text.Encoding]::UTF8.GetBytes($canonicalText)
$actualDigest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($digestBytes)).ToLowerInvariant()
if ($actualDigest -ne ([string]$manifest.projectionDigestSha256).ToLowerInvariant()) {
    throw 'Projected-source canonical digest does not match the manifest.'
}

$textExtensions = @('.ps1','.psm1','.psd1','.sh','.bash','.yml','.yaml','.json','.ini','.txt','.md','.cpp','.cc','.cxx','.hpp','.hh','.h','.c','.cmake','.nsi','.xml','.go','.mod','.sum','.rc','.manifest')
$secretPatterns = @(
    '-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',
    '\bghp_[A-Za-z0-9]{20,}\b',
    '\bgithub_pat_[A-Za-z0-9_]{20,}\b',
    '\bAKIA[0-9A-Z]{16}\b'
)
foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -File -Recurse) {
    $ext = $file.Extension.ToLowerInvariant()
    if ($textExtensions -contains $ext -or $file.Name -in @('CMakeLists.txt','LICENSE')) {
        $text = Get-Content -LiteralPath $file.FullName -Raw
        foreach ($pattern in $secretPatterns) {
            if ($text -match $pattern) { throw "Credential-like content found in projected source file: $($file.FullName)" }
        }
    }
}

Write-Host "Projected source validation passed."
Write-Host "Private source commit: $($manifest.sourceCommitSha)"
Write-Host "Files: $($manifest.fileCount)"
Write-Host "Projection digest: $actualDigest"
