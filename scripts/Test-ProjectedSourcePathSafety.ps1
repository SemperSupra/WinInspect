#Requires -Version 7.0
[CmdletBinding()]
param([string] $RepositoryRoot = (Resolve-Path '.').Path)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$validator = Join-Path $RepositoryRoot 'scripts/Test-ProjectedSource.ps1'
if (-not (Test-Path -LiteralPath $validator -PathType Leaf)) { throw "Projected-source validator is missing: $validator" }

function Write-ReceiptCase {
    param(
        [Parameter(Mandatory)][string] $Root,
        [Parameter(Mandatory)][string] $RelativePath,
        [ValidateSet(1,2)][int] $SchemaVersion = 1,
        [switch] $LeakPrivateSourceSha
    )

    $sourceRoot = Join-Path $Root 'source'
    $manifestRoot = Join-Path $Root '.projection'
    $filePath = Join-Path $sourceRoot (($RelativePath -replace '\\', [IO.Path]::DirectorySeparatorChar) -replace '/', [IO.Path]::DirectorySeparatorChar)
    $parent = Split-Path -Parent $filePath
    New-Item -ItemType Directory -Force -Path $parent, $manifestRoot | Out-Null
    [IO.File]::WriteAllText($filePath, "synthetic`n", [Text.UTF8Encoding]::new($false))
    $item = Get-Item -LiteralPath $filePath
    $sha = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $normalized = $RelativePath.Replace('\', '/')
    $canonical = "$normalized`t$($item.Length)`t$sha`tfalse`n"
    $digest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($canonical))).ToLowerInvariant()

    $receipt = [ordered]@{
        schemaVersion = $SchemaVersion
        projectionPolicyVersion = 1
        sourceAuthority = 'private-development'
        authoritativeCandidate = $true
        projectionDigestSha256 = $digest
        fileCount = 1
        files = @([ordered]@{
            path = $RelativePath
            sha256 = $sha
            bytes = [int64]$item.Length
            executable = $false
        })
    }
    if ($SchemaVersion -eq 1) {
        $receipt['sourceCommitSha'] = ('a' * 40)
    } else {
        $receipt['projectionPolicySha256'] = ('b' * 64)
        if ($LeakPrivateSourceSha) { $receipt['sourceCommitSha'] = ('a' * 40) }
    }

    $receipt | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $manifestRoot 'source-manifest.json') -Encoding utf8NoBOM
}

function Assert-Rejected {
    param(
        [Parameter(Mandatory)][string] $RelativePath,
        [Parameter(Mandatory)][string] $ExpectedMessage,
        [ValidateSet(1,2)][int] $SchemaVersion = 1,
        [switch] $LeakPrivateSourceSha
    )

    $caseRoot = Join-Path ([IO.Path]::GetTempPath()) ('winspect-path-safety-' + [guid]::NewGuid().ToString('N'))
    try {
        Write-ReceiptCase -Root $caseRoot -RelativePath $RelativePath -SchemaVersion $SchemaVersion -LeakPrivateSourceSha:$LeakPrivateSourceSha
        try {
            & $validator -RepositoryRoot $caseRoot
            throw "Unsafe projected receipt was accepted: schema=$SchemaVersion path=$RelativePath"
        } catch {
            if ($_.Exception.Message -notmatch [regex]::Escape($ExpectedMessage)) { throw }
        }
    } finally {
        if (Test-Path -LiteralPath $caseRoot) { Remove-Item -LiteralPath $caseRoot -Recurse -Force }
    }
}

# Positive v2 case: public receipt binds policy/digest/file commitments while
# deliberately omitting the private development commit identity.
$v2Root = Join-Path ([IO.Path]::GetTempPath()) ('winspect-v2-receipt-' + [guid]::NewGuid().ToString('N'))
try {
    Write-ReceiptCase -Root $v2Root -RelativePath 'CMakeLists.txt' -SchemaVersion 2
    & $validator -RepositoryRoot $v2Root
} finally {
    if (Test-Path -LiteralPath $v2Root) { Remove-Item -LiteralPath $v2Root -Recurse -Force }
}

Assert-Rejected -RelativePath '.github/workflows/private.yml' -ExpectedMessage 'Private/control-plane path is forbidden'
Assert-Rejected -RelativePath '.github\workflows\private.yml' -ExpectedMessage 'Private/control-plane path is forbidden'
Assert-Rejected -RelativePath '.claude/agent.md' -ExpectedMessage 'Private/control-plane path is forbidden'
Assert-Rejected -RelativePath 'CMakeLists.txt' -SchemaVersion 2 -LeakPrivateSourceSha -ExpectedMessage 'must not expose the private source commit SHA'

# The corrected v2 contract must not recreate the broad Loop 1 disclosure under
# a new receipt schema.
foreach ($privateTestPath in @(
    'core/tests/test_trace_replay.cpp',
    'clients/gui/tests/test_viewmodel.cpp',
    'clients/mcp/src/tools.test.ts',
    'clients/sdk-python/tests/test_client.py',
    'clients/sdk-typescript/src/index.test.ts',
    'third_party/doctest/doctest.h',
    'third_party/rapidcheck/rapidcheck.hpp',
    'daemon/src/test_discovery.cpp',
    'cmake/PrivateTests.cmake'
)) {
    Assert-Rejected -RelativePath $privateTestPath -SchemaVersion 2 -ExpectedMessage 'Schema v2 private validation/evaluator path is forbidden'
}

Write-Host 'Projected-source path/schema/private-boundary regression tests passed.'
