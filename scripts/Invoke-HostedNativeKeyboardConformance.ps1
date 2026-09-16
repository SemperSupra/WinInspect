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
    public const int GWL_STYLE = -16;
    public const long WS_TABSTOP = 0x00010000L;

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW", SetLastError = true)]
    public static extern IntPtr GetWindowLongPtr(IntPtr hWnd, int nIndex);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("kernel32.dll")]
    public static extern uint GetCurrentThreadId();

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr SetFocus(IntPtr hWnd);
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
        is_keyboard_focusable = $c.IsKeyboardFocusable
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

    $connectHwnd = [IntPtr]::new([int64]$connect.Current.NativeWindowHandle)
    if ($connectHwnd -eq [IntPtr]::Zero) { throw 'Connect UIA element did not expose a native window handle.' }
    $connectStyle = [KeyboardNative]::GetWindowLongPtr($connectHwnd, [KeyboardNative]::GWL_STYLE).ToInt64()
    $nativeTabStop = (($connectStyle -band [KeyboardNative]::WS_TABSTOP) -ne 0)
    if (-not $nativeTabStop) { throw 'Connect does not carry the native WS_TABSTOP style after keyboard-access normalization.' }

    [KeyboardNative]::SetForegroundWindow($hwnd) | Out-Null
    [uint32]$targetPid = 0
    $targetThread = [KeyboardNative]::GetWindowThreadProcessId($connectHwnd, [ref]$targetPid)
    $currentThread = [KeyboardNative]::GetCurrentThreadId()
    if ($targetThread -eq 0) { throw 'Could not resolve GUI thread for Connect.' }

    $attached = $false
    try {
        if ($targetThread -ne $currentThread) {
            $attached = [KeyboardNative]::AttachThreadInput($currentThread, $targetThread, $true)
            if (-not $attached) { throw 'AttachThreadInput failed while establishing hosted keyboard focus.' }
        }
        [KeyboardNative]::SetFocus($connectHwnd) | Out-Null
    }
    finally {
        if ($attached) {
            [KeyboardNative]::AttachThreadInput($currentThread, $targetThread, $false) | Out-Null
        }
    }

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
        connect_native_style = ('0x{0:X}' -f $connectStyle)
        connect_native_tabstop = $nativeTabStop
        connect_uia_keyboard_focusable = $connect.Current.IsKeyboardFocusable
        focus_establishment = 'AttachThreadInput+SetFocus'
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
