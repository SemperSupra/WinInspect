#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $InstallerPath,

    [string] $ExpectedInstallDirectory = (Join-Path $env:LOCALAPPDATA 'WinInspect'),
    [string] $UninstallKeyName = 'WinInspect',
    [string[]] $ExpectedExecutables = @('wininspectd.exe', 'wininspect.exe', 'wininspect-gui.exe'),
    [int] $TimeoutSeconds = 180,
    [int] $CleanupTimeoutSeconds = 15,
    [string] $EvidencePath = (Join-Path $PWD 'installer-lifecycle-evidence.json')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Get-InstallState {
    $hkcu = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$UninstallKeyName"
    $hklm = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$UninstallKeyName"
    $hklmWow = "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\$UninstallKeyName"

    [ordered]@{
        installDirectoryPresent = Test-Path -LiteralPath $ExpectedInstallDirectory -PathType Container
        files = if (Test-Path -LiteralPath $ExpectedInstallDirectory -PathType Container) {
            @(Get-ChildItem -LiteralPath $ExpectedInstallDirectory -Force -ErrorAction SilentlyContinue |
                Select-Object Name, Length, LastWriteTimeUtc)
        } else { @() }
        hkcuUninstallPresent = Test-Path -LiteralPath $hkcu
        hklmUninstallPresent = Test-Path -LiteralPath $hklm
        hklmWowUninstallPresent = Test-Path -LiteralPath $hklmWow
    }
}

function Get-InstallerDiagnostics {
    $interesting = Get-CimInstance Win32_Process | Where-Object {
        $_.Name -match '^(WinInspect|vc_redist|installer|uninstall|Un\.exe).*' -or
        $_.CommandLine -match 'WinInspect|vc_redist|~nsu'
    } | Select-Object ProcessId, ParentProcessId, Name, ExecutablePath, CommandLine

    $windows = Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.MainWindowHandle -ne 0 -and $_.ProcessName -match 'WinInspect|vc_redist|installer|uninstall|^Un$'
    } | Select-Object Id, ProcessName, MainWindowTitle, Responding

    [ordered]@{
        capturedAtUtc = [DateTime]::UtcNow.ToString('o')
        processes = @($interesting)
        visibleWindows = @($windows)
        state = Get-InstallState
    }
}

function Invoke-BoundedInstaller {
    param(
        [Parameter(Mandatory)][string] $FilePath,
        [Parameter(Mandatory)][string[]] $Arguments,
        [Parameter(Mandatory)][string] $Operation
    )

    Write-Host "BEGIN $Operation"
    Write-Host "Executable: $FilePath"
    Write-Host "Arguments: $($Arguments -join ' ')"

    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $diagnostics = Get-InstallerDiagnostics
        $diagnostics | ConvertTo-Json -Depth 8 | Write-Host
        try {
            & taskkill.exe /PID $process.Id /T /F | Write-Host
        } catch {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
        throw "$Operation timed out after ${TimeoutSeconds}s."
    }

    $process.WaitForExit()
    Write-Host "END $Operation (exit $($process.ExitCode))"
    if ($process.ExitCode -ne 0) {
        throw "$Operation failed with exit code $($process.ExitCode)."
    }
}

function Assert-InstalledState {
    param([string] $Stage)

    if (-not (Test-Path -LiteralPath $ExpectedInstallDirectory -PathType Container)) {
        throw "${Stage}: expected install directory is absent: $ExpectedInstallDirectory"
    }

    foreach ($exe in $ExpectedExecutables) {
        $path = Join-Path $ExpectedInstallDirectory $exe
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "${Stage}: expected executable is absent: $path"
        }
    }

    $hkcu = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$UninstallKeyName"
    if (-not (Test-Path -LiteralPath $hkcu)) {
        throw "${Stage}: expected HKCU uninstall registration is absent: $hkcu"
    }

    foreach ($path in @(
        "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$UninstallKeyName",
        "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\$UninstallKeyName"
    )) {
        if (Test-Path -LiteralPath $path) {
            throw "${Stage}: unexpected machine-scope uninstall registration exists: $path"
        }
    }
}

function Wait-ForUninstalledState {
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $lastState = $null

    while ($stopwatch.Elapsed.TotalSeconds -lt $CleanupTimeoutSeconds) {
        $lastState = Get-InstallState
        $clean = -not $lastState.installDirectoryPresent -and
            -not $lastState.hkcuUninstallPresent -and
            -not $lastState.hklmUninstallPresent -and
            -not $lastState.hklmWowUninstallPresent
        if ($clean) {
            $stopwatch.Stop()
            Write-Host ("Uninstall converged to clean state after {0:N3}s." -f $stopwatch.Elapsed.TotalSeconds)
            return $stopwatch.Elapsed.TotalMilliseconds
        }
        Start-Sleep -Milliseconds 200
    }

    $stopwatch.Stop()
    throw "Uninstall did not converge to a clean state within ${CleanupTimeoutSeconds}s. Last state: $($lastState | ConvertTo-Json -Compress -Depth 5)"
}

$resolvedInstaller = (Resolve-Path -LiteralPath $InstallerPath).Path
if ($TimeoutSeconds -lt 30 -or $TimeoutSeconds -gt 600) {
    throw 'TimeoutSeconds must be between 30 and 600 seconds.'
}
if ($CleanupTimeoutSeconds -lt 1 -or $CleanupTimeoutSeconds -gt 60) {
    throw 'CleanupTimeoutSeconds must be between 1 and 60 seconds.'
}

$evidence = [ordered]@{
    schemaVersion = 1
    installer = $resolvedInstaller
    installerSha256 = (Get-FileHash -LiteralPath $resolvedInstaller -Algorithm SHA256).Hash.ToLowerInvariant()
    runner = [ordered]@{
        os = [System.Environment]::OSVersion.VersionString
        architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    }
    checks = [ordered]@{}
    verdict = 'failed'
}

try {
    Invoke-BoundedInstaller -FilePath $resolvedInstaller -Arguments @('/S') -Operation 'silent-install'
    Assert-InstalledState -Stage 'Silent install'
    $evidence.checks.silentInstall = 'passed'
    $evidence.checks.userScopeRegistration = 'passed'
    $evidence.checks.machineScopeRegistrationAbsent = 'passed'

    Invoke-BoundedInstaller -FilePath $resolvedInstaller -Arguments @('/S') -Operation 'repeat-silent-install'
    Assert-InstalledState -Stage 'Repeat silent install'
    $evidence.checks.repeatSilentInstall = 'passed'

    $uninstaller = Join-Path $ExpectedInstallDirectory 'uninstall.exe'
    if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
        throw "Expected uninstaller is absent: $uninstaller"
    }
    Invoke-BoundedInstaller -FilePath $uninstaller -Arguments @('/S') -Operation 'silent-uninstall'
    $cleanupMs = Wait-ForUninstalledState
    $evidence.checks.silentUninstall = 'passed'
    $evidence.checks.completeCleanup = 'passed'
    $evidence.cleanupConvergenceMilliseconds = [math]::Round($cleanupMs, 1)

    $evidence.verdict = 'passed'
    Write-Host 'Packaged installer lifecycle proof passed.'
} catch {
    $evidence.failure = $_.Exception.Message
    $evidence.diagnostics = Get-InstallerDiagnostics
    throw
} finally {
    $evidence | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $EvidencePath -Encoding utf8NoBOM
    Write-Host "Lifecycle evidence: $EvidencePath"
}
