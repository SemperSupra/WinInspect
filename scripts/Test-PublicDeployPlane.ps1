#Requires -Version 7.0
[CmdletBinding()]
param([string] $RepositoryRoot = (Resolve-Path '.').Path)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$required = @(
    'README.md',
    'docs/public-build-deploy-architecture.md',
    'docs/zero-budget-development.md',
    'scripts/Test-ProjectedSource.ps1',
    '.github/workflows/projected-source-ci.yml',
    '.github/workflows/release.yml'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) {
        throw "Public deploy plane lost required artifact: $relative"
    }
}

$workflowRoot = Join-Path $root '.github/workflows'
$usesPattern = '(?m)^\s*-?\s*uses:\s*([^\s#]+)'
foreach ($file in Get-ChildItem -LiteralPath $workflowRoot -File -Include *.yml,*.yaml) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($match in [regex]::Matches($text, $usesPattern)) {
        $reference = $match.Groups[1].Value.Trim('"', "'")
        if ($reference.StartsWith('./', [StringComparison]::Ordinal) -or $reference.StartsWith('docker://', [StringComparison]::OrdinalIgnoreCase)) { continue }
        if ($reference -notmatch '@[0-9a-fA-F]{40}$') {
            throw "External action is not pinned to a full commit SHA in $($file.Name): $reference"
        }
    }

    foreach ($forbidden in @(
        'WinInspect-private',
        'windows-package-foundry-private',
        'pull_request_target',
        'secrets.'
    )) {
        if ($text.Contains($forbidden, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Public deploy workflow contains forbidden private/credential capability marker '$forbidden': $($file.Name)"
        }
    }
}

$releaseText = Get-Content -LiteralPath (Join-Path $workflowRoot 'release.yml') -Raw
foreach ($marker in @(
    './scripts/Test-ProjectedSource.ps1',
    '.projection/source-manifest.json',
    'WinInspect-source-projection-',
    'Test-InstallerLifecycle.ps1',
    'Generate Foundry public release trust envelope',
    'SemperSupra/windows-package-foundry/actions/release-trust@7076219cfba8265b29e00e111b153b3114256ce9',
    'gh attestation verify',
    'Native-client lifecycle and private Foundry promotion remain separate gates.'
)) {
    if (-not $releaseText.Contains($marker, [StringComparison]::Ordinal)) {
        throw "Projected release workflow lost required boundary/trust marker: $marker"
    }
}

$readme = Get-Content -LiteralPath (Join-Path $root 'README.md') -Raw
foreach ($marker in @(
    'public build/deployment/release plane',
    'constructive sanitization/projection process',
    '.projection/source-manifest.json',
    'Do not implement product fixes directly here'
)) {
    if (-not $readme.Contains($marker, [StringComparison]::Ordinal)) {
        throw "Public deploy README lost required role marker: $marker"
    }
}

Write-Host 'Public WinInspect deploy-plane architecture validation passed.'
