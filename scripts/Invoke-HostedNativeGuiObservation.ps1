param(
    [Parameter(Mandatory = $true)]
    [string]$GuiPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [int]$TimeoutSeconds = 20
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Write-JsonFile {
    param([string]$Path, [object]$Value, [int]$Depth = 12)
    $Value | ConvertTo-Json -Depth $Depth | Set-Content -LiteralPath $Path -Encoding utf8
}

$observation = [ordered]@{
    schema_version = 1
    timestamp_utc = [DateTime]::UtcNow.ToString('o')
    gui_path = (Resolve-Path -LiteralPath $GuiPath).Path
    runner = [ordered]@{}
    target = [ordered]@{}
    semantic_observation = [ordered]@{ status = 'UNAVAILABLE'; reason = $null; element_count = 0 }
    window_capture = [ordered]@{ status = 'UNAVAILABLE'; reason = $null; path = $null; sha256 = $null }
    interactive_input = [ordered]@{
        status = 'NOT_TESTED'
        reason = 'Hosted observation rep does not synthesize user input; interactive acceptance remains a separate gate.'
    }
}

try {
    $os = Get-CimInstance Win32_OperatingSystem
    $observation.runner.os_caption = $os.Caption
    $observation.runner.os_version = $os.Version
    $observation.runner.os_build = $os.BuildNumber
    $observation.runner.os_architecture = $os.OSArchitecture
} catch {
    $observation.runner.os_error = $_.Exception.Message
}

$observation.runner.processor_architecture = $env:PROCESSOR_ARCHITECTURE
$observation.runner.processor_architew6432 = $env:PROCESSOR_ARCHITEW6432
$observation.runner.runner_arch = $env:RUNNER_ARCH
$observation.runner.runner_os = $env:RUNNER_OS
$observation.runner.image_os = $env:ImageOS
$observation.runner.image_version = $env:ImageVersion
$observation.runner.session_id = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
$observation.runner.user_interactive = [Environment]::UserInteractive
$observation.runner.is_64bit_process = [Environment]::Is64BitProcess
$observation.runner.is_64bit_os = [Environment]::Is64BitOperatingSystem

try {
    $observation.runner.quser = (& quser 2>&1 | Out-String).Trim()
} catch {
    $observation.runner.quser = "UNAVAILABLE: $($_.Exception.Message)"
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class HostedNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool IsWow64Process2(IntPtr hProcess, out ushort processMachine, out ushort nativeMachine);
}
'@

$process = $null
try {
    $process = Start-Process -FilePath $GuiPath -PassThru
    $observation.target.pid = $process.Id
    $observation.target.session_id = $process.SessionId

    try {
        [UInt16]$processMachine = 0
        [UInt16]$nativeMachine = 0
        if ([HostedNative]::IsWow64Process2($process.Handle, [ref]$processMachine, [ref]$nativeMachine)) {
            $observation.target.process_machine = ('0x{0:X4}' -f $processMachine)
            $observation.target.native_machine = ('0x{0:X4}' -f $nativeMachine)
            $observation.target.emulated = ($processMachine -ne 0)
        } else {
            $observation.target.architecture_probe_error = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        }
    } catch {
        $observation.target.architecture_probe_error = $_.Exception.Message
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $hwnd = [IntPtr]::Zero
    do {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        $hwnd = $process.MainWindowHandle
    } while ($hwnd -eq [IntPtr]::Zero -and -not $process.HasExited -and [DateTime]::UtcNow -lt $deadline)

    $observation.target.exited_before_observation = $process.HasExited
    if ($process.HasExited) {
        $observation.target.exit_code = $process.ExitCode
    }
    $observation.target.main_window_handle = ('0x{0:X}' -f $hwnd.ToInt64())

    if ($hwnd -eq [IntPtr]::Zero) {
        $reason = if ($process.HasExited) { "GUI exited before exposing a main window (exit $($process.ExitCode))." } else { 'No MainWindowHandle became visible in the hosted runner session.' }
        $observation.semantic_observation.reason = $reason
        $observation.window_capture.reason = $reason
    } else {
        # UI Automation: semantic/native observation that does not require synthetic input.
        try {
            Add-Type -AssemblyName UIAutomationClient
            Add-Type -AssemblyName UIAutomationTypes

            $root = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
            if ($null -eq $root) {
                throw 'AutomationElement.FromHandle returned null.'
            }

            $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
            $elements = [System.Collections.Generic.List[object]]::new()
            $maxElements = 750
            $maxDepth = 8

            function Add-AutomationNode {
                param(
                    [System.Windows.Automation.AutomationElement]$Element,
                    [int]$Depth,
                    [string]$ParentRuntimeId
                )
                if ($null -eq $Element -or $Depth -gt $maxDepth -or $elements.Count -ge $maxElements) { return }

                $current = $Element.Current
                $rid = try { ($Element.GetRuntimeId() -join '.') } catch { '' }
                $rect = $current.BoundingRectangle
                $elements.Add([ordered]@{
                    runtime_id = $rid
                    parent_runtime_id = $ParentRuntimeId
                    depth = $Depth
                    name = $current.Name
                    automation_id = $current.AutomationId
                    class_name = $current.ClassName
                    framework_id = $current.FrameworkId
                    control_type = $current.ControlType.ProgrammaticName
                    process_id = $current.ProcessId
                    enabled = $current.IsEnabled
                    offscreen = $current.IsOffscreen
                    bounds = [ordered]@{ x = $rect.X; y = $rect.Y; width = $rect.Width; height = $rect.Height }
                }) | Out-Null

                $child = $walker.GetFirstChild($Element)
                while ($null -ne $child -and $elements.Count -lt $maxElements) {
                    Add-AutomationNode -Element $child -Depth ($Depth + 1) -ParentRuntimeId $rid
                    $child = $walker.GetNextSibling($child)
                }
            }

            Add-AutomationNode -Element $root -Depth 0 -ParentRuntimeId ''
            $uiaPath = Join-Path $OutputDirectory 'uia-tree.json'
            Write-JsonFile -Path $uiaPath -Value $elements -Depth 10
            $observation.semantic_observation.status = if ($elements.Count -gt 0) { 'PASS' } else { 'UNAVAILABLE' }
            $observation.semantic_observation.element_count = $elements.Count
            if ($elements.Count -eq 0) { $observation.semantic_observation.reason = 'UI Automation returned an empty control-view tree.' }
        } catch {
            $observation.semantic_observation.status = 'UNAVAILABLE'
            $observation.semantic_observation.reason = $_.Exception.Message
        }

        # Pixel observation: attempt per-window PrintWindow capture. This is deliberately
        # classified separately from semantic observation and from interactive input.
        try {
            Add-Type -AssemblyName System.Drawing
            $rect = New-Object HostedNative+RECT
            if (-not [HostedNative]::GetWindowRect($hwnd, [ref]$rect)) {
                throw "GetWindowRect failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
            }
            $width = $rect.Right - $rect.Left
            $height = $rect.Bottom - $rect.Top
            if ($width -le 0 -or $height -le 0) { throw "Invalid window bounds ${width}x${height}." }

            $bitmap = [System.Drawing.Bitmap]::new($width, $height)
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $hdc = $graphics.GetHdc()
                try {
                    $printOk = [HostedNative]::PrintWindow($hwnd, $hdc, 2) # PW_RENDERFULLCONTENT
                } finally {
                    $graphics.ReleaseHdc($hdc)
                }
            } finally {
                $graphics.Dispose()
            }

            $capturePath = Join-Path $OutputDirectory 'wininspect-gui.png'
            $bitmap.Save($capturePath, [System.Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()

            $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $capturePath).Hash.ToLowerInvariant()
            $observation.window_capture.path = 'wininspect-gui.png'
            $observation.window_capture.sha256 = $hash
            $observation.window_capture.width = $width
            $observation.window_capture.height = $height
            if ($printOk) {
                $observation.window_capture.status = 'PASS'
            } else {
                $observation.window_capture.status = 'UNRELIABLE'
                $observation.window_capture.reason = "PrintWindow returned false; PNG retained for diagnosis. Win32 error: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
            }
        } catch {
            $observation.window_capture.status = 'UNAVAILABLE'
            $observation.window_capture.reason = $_.Exception.Message
        }
    }
} catch {
    $observation.target.launch_error = $_.Exception.Message
    $observation.semantic_observation.reason = 'GUI launch failed.'
    $observation.window_capture.reason = 'GUI launch failed.'
} finally {
    if ($null -ne $process) {
        try {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
        } catch {}
        $process.Dispose()
    }
}

$summaryPath = Join-Path $OutputDirectory 'observation.json'
Write-JsonFile -Path $summaryPath -Value $observation -Depth 12

Write-Host "semantic_observation=$($observation.semantic_observation.status)"
Write-Host "window_capture=$($observation.window_capture.status)"
Write-Host "interactive_input=$($observation.interactive_input.status)"
Write-Host "evidence=$summaryPath"
