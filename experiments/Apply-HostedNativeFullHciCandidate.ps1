$ErrorActionPreference = 'Stop'

# This script is intentionally only a composition point. Each transform remains
# independently inspectable and qualified; this file fixes their order so a single
# candidate can be built and exercised for interaction effects.
$steps = @(
    'Apply-HostedNativeHciRepair.ps1',
    'Normalize-HostedNativeHciLayout.ps1',
    'Normalize-HostedNativeKeyboardAccess.ps1',
    'Normalize-HostedNativeAccessibleNames.ps1',
    'Normalize-HostedNativeHighContrast.ps1',
    'Normalize-HostedNativeDpiScaling.ps1',
    'Normalize-HostedNativeAllPanelDpi.ps1'
)

foreach ($step in $steps) {
    $path = Join-Path $PSScriptRoot $step
    if (-not (Test-Path -LiteralPath $path)) { throw "Required HCI transform missing: $step" }
    # PowerShell scripts signal failure by throwing under ErrorActionPreference=Stop.
    # Do not inspect inherited $LASTEXITCODE here: a child script may invoke native
    # commands internally and leave a stale value even after completing successfully.
    & $path
}

git diff --check
if ($LASTEXITCODE -ne 0) { throw 'Full HCI composition introduced whitespace errors.' }

Write-Host ('Full hosted-native HCI candidate composed: ' + ($steps -join ' -> '))
