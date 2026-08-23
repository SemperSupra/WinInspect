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
        [Parameter(Mandatory)][string] $RelativePath
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
        schemaVersion = 1
        projectionPolicyVersion = 1
        sourceAuthority = 'private-development'
        sourceCommitSha = ('a' * 40)
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
    $receipt | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $manifestRoot 'source-manifest.json') -Encoding utf8NoBOM
}

function Assert-Rejected {
    param(
        [Parameter(Mandatory)][string] $RelativePath,
        [Parameter(Mandatory)][string] $ExpectedMessage
    )

    $caseRoot = Join-Path ([IO.Path]::GetTempPath()) ('winspect-path-safety-' + [guid]::NewGuid().ToString('N'))
    try {
        Write-ReceiptCase -Root $caseRoot -RelativePath $RelativePath
        try {
            & $validator -RepositoryRoot $caseRoot
            throw "Unsafe projected path was accepted: $RelativePath"
        } catch {
            if ($_.Exception.Message -notmatch [regex]::Escape($ExpectedMessage)) { throw }
        }
    } finally {
        if (Test-Path -LiteralPath $caseRoot) { Remove-Item -LiteralPath $caseRoot -Recurse -Force }
    }
}

Assert-Rejected -RelativePath '.github/workflows/private.yml' -ExpectedMessage 'Private/control-plane path is forbidden'
Assert-Rejected -RelativePath '.github\workflows\private.yml' -ExpectedMessage 'Private/control-plane path is forbidden'
Assert-Rejected -RelativePath '.claude/agent.md' -ExpectedMessage 'Private/control-plane path is forbidden'

Write-Host 'Projected-source path safety regression tests passed.'
