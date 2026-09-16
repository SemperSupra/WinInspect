param(
    [Parameter(Mandatory = $true)][string]$GuiPath,
    [string]$OutputPath = 'evidence/keyboard-conformance.json',
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
$gui = (Resolve-Path -LiteralPath $GuiPath).Path
$outputFull = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputFull) | Out-Null

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class KeyboardNative {
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
}
'@

function Get-FocusRecord {
    $e = [System.Windows.Automation.AutomationElement]::FocusedElement
    if ($null -eq $e) { return $null }
    $c = $e.Current
    $rid = try { $e.GetRuntimeId() -join '.' } catch { '' }
    [ordered]@{
        runtime_id = $rid
        name = $c.Name
        automation_id = $c.AutomationId
        class_name = $c.ClassName
        control_type = $c.ControlType.ProgrammaticName
        enabled = $c.IsEnabled
        offscreen = $c.IsOffscreen
        native_window_handle = $c.NativeWindowHandle
    }
}

$process = Start-Process -FilePath $gui -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $hwnd = [IntPtr]::Zero
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        $hwnd = $process.MainWindowHandle
    } while ($hwnd -eq [IntPtr]::Zero -and -not $process.HasExited -and [DateTime]::UtcNow -lt $deadline)

    if ($process.HasExited) { throw "GUI exited before keyboard probe with code $($process.ExitCode)." }
    if ($hwnd -eq [IntPtr]::Zero) { throw 'GUI did not expose a main window for keyboard probe.' }

    $root = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
    if ($null -eq $root) { throw 'UI Automation could not bind to the GUI root.' }

    $connectCondition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::NameProperty, 'Connect')
    $connect = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $connectCondition)
    if ($null -eq $connect) { throw 'Connect control not found through UI Automation.' }
    if (-not $connect.Current.IsKeyboardFocusable) { throw 'Connect is not keyboard-focusable after keyboard-access normalization.' }

    [KeyboardNative]::SetForegroundWindow($hwnd) | Out-Null
    $connect.SetFocus()
    Start-Sleep -Milliseconds 200
    $initial = Get-FocusRecord
    if ($null -eq $initial -or $initial.name -ne 'Connect') {
        throw "Could not establish Connect as initial keyboard focus; observed '$($initial.name)'."
    }

    [System.Windows.Forms.SendKeys]::SendWait('{TAB}')
    Start-Sleep -Milliseconds 250
    $forward = Get-FocusRecord
    if ($null -eq $forward) { throw 'No focused element after Tab.' }
    if ($forward.runtime_id -eq $initial.runtime_id) {
        throw 'Tab did not advance keyboard focus from Connect.'
    }

    [System.Windows.Forms.SendKeys]::SendWait('+{TAB}')
    Start-Sleep -Milliseconds 250
    $reverse = Get-FocusRecord
    if ($null -eq $reverse) { throw 'No focused element after Shift+Tab.' }
    if ($reverse.runtime_id -ne $initial.runtime_id) {
        throw "Shift+Tab did not return focus to Connect; observed '$($reverse.name)' ($($reverse.control_type))."
    }

    $evidence = [ordered]@{
        schema_version = 1
        timestamp_utc = [DateTime]::UtcNow.ToString('o')
        gui_path = $gui
        process_id = $process.Id
        main_window_handle = ('0x{0:X}' -f $hwnd.ToInt64())
        initial_focus = $initial
        after_tab = $forward
        after_shift_tab = $reverse
        status = 'PASS'
        boundary = 'Hosted-native keyboard traversal evidence only; not controlled interactive WinBot/native-desktop acceptance.'
    }
    $evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outputFull -Encoding utf8
    Get-Content -LiteralPath $outputFull
}
finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    $process.Dispose()
}
