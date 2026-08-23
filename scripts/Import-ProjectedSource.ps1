#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $ProjectionRoot,
    [string] $RepositoryRoot = (Resolve-Path '.').Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$projection = (Resolve-Path -LiteralPath $ProjectionRoot).Path
$repo = (Resolve-Path -LiteralPath $RepositoryRoot).Path
if ($projection.Equals($repo, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'ProjectionRoot must be separate from the public repository checkout.'
}

foreach ($required in @('source', '.projection/source-manifest.json')) {
    $path = Join-Path $projection $required
    if ($required -eq 'source') {
        if (-not (Test-Path -LiteralPath $path -PathType Container)) { throw "Projection input is incomplete: $required" }
    } elseif (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Projection input is incomplete: $required"
    }
}

$validator = Join-Path $repo 'scripts/Test-ProjectedSource.ps1'
if (-not (Test-Path -LiteralPath $validator -PathType Leaf)) { throw 'Public projected-source validator is absent from the destination repository.' }

$git = Get-Command git -ErrorAction Stop
$remote = (& $git.Source -C $repo remote get-url origin 2>$null).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Unable to resolve public repository origin remote.' }
$global:LASTEXITCODE = 0
if ($remote -notmatch '(?i)(github\.com[:/])SemperSupra/WinInspect(?:\.git)?$') {
    throw "Refusing to import projection into unexpected Git remote: $remote"
}

# Validate the generated projection before touching the public checkout.
& $validator -RepositoryRoot $projection

$statusBefore = @(& $git.Source -C $repo status --porcelain -- source .projection)
if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect existing public projection state.' }
$global:LASTEXITCODE = 0
if ($statusBefore.Count -gt 0) {
    throw 'Public source/.projection already contain uncommitted changes. Commit, stash, or discard them before importing a new projection.'
}

$temp = Join-Path ([IO.Path]::GetTempPath()) ('winspect-public-import-' + [guid]::NewGuid().ToString('N'))
$incoming = Join-Path $temp 'incoming'
$backup = Join-Path $temp 'backup'
New-Item -ItemType Directory -Force -Path $incoming, $backup | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $projection 'source') -Destination (Join-Path $incoming 'source') -Recurse -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $incoming '.projection') | Out-Null
    Copy-Item -LiteralPath (Join-Path $projection '.projection/source-manifest.json') -Destination (Join-Path $incoming '.projection/source-manifest.json') -Force
    & $validator -RepositoryRoot $incoming

    foreach ($relative in @('source', '.projection')) {
        $current = Join-Path $repo $relative
        if (Test-Path -LiteralPath $current) {
            Copy-Item -LiteralPath $current -Destination (Join-Path $backup $relative) -Recurse -Force
        }
    }

    try {
        foreach ($relative in @('source', '.projection')) {
            $destination = Join-Path $repo $relative
            if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
            Move-Item -LiteralPath (Join-Path $incoming $relative) -Destination $destination
        }

        & $validator -RepositoryRoot $repo
    } catch {
        $failure = $_
        Write-Warning 'Imported projection failed final validation; restoring previous clean projection.'
        foreach ($relative in @('source', '.projection')) {
            $destination = Join-Path $repo $relative
            if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
            $saved = Join-Path $backup $relative
            if (Test-Path -LiteralPath $saved) { Move-Item -LiteralPath $saved -Destination $destination }
        }
        throw $failure
    }

    $manifest = Get-Content -LiteralPath (Join-Path $repo '.projection/source-manifest.json') -Raw | ConvertFrom-Json
    Write-Host 'Imported WinInspect source projection.'
    Write-Host "Receipt schema: $($manifest.schemaVersion)"
    if ([int]$manifest.schemaVersion -eq 1) {
        Write-Host "Legacy private source commit: $($manifest.sourceCommitSha)"
    } else {
        Write-Host 'Private source identity is intentionally redacted from schema v2 public receipt.'
        Write-Host "Projection policy SHA-256: $($manifest.projectionPolicySha256)"
    }
    Write-Host "Projection digest: $($manifest.projectionDigestSha256)"
    Write-Host 'Review the git diff, commit source/ + .projection/ on a projection branch, and push it for public CI.'
    & $git.Source -C $repo status --short -- source .projection | Write-Host
    $global:LASTEXITCODE = 0
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue }
}
