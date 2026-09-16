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
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class KeyboardNative {
    public const int GWL_STYLE = -16;
    public const long WS_TABSTOP = 0x00010000L;
    public const uint WM_KEYDOWN = 0x0100;
    public const uint WM_KEYUP = 0x0101;
    public const int VK_TAB = 0x09;
    public const int VK_SHIFT = 0x10;

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left, Top, Right, Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct GUITHREADINFO {
        public uint cbSize;
        public uint flags;
        public IntPtr hwndActive;
        public IntPtr hwndFocus;
        public IntPtr hwndCapture;
        public IntPtr hwndMenuOwner;
        public IntPtr hwndMoveSize;
        public IntPtr hwndCaret;
        public RECT rcCaret;
    }

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

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetGUIThreadInfo(uint idThread, ref GUITHREADINFO info);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetKeyboardState([Out] byte[] state);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetKeyboardState(byte[] state);
}
'@

function Get-ThreadFocusHwnd([uint32]$ThreadId) {
    $info = [KeyboardNative+GUITHREADINFO]::new()
    $info.cbSize = [Runtime.InteropServices.Marshal]::SizeOf([type][KeyboardNative+GUITHREADINFO])
    if (-not [KeyboardNative]::GetGUIThreadInfo($ThreadId, [ref]$info)) {
        throw "GetGUIThreadInfo failed for GUI thread $ThreadId."
    }
    return $info.hwndFocus
}

function Get-ElementRecordFromHwnd([IntPtr]$WindowHandle) {
    if ($WindowHandle -eq [IntPtr]::Zero) { return $null }
    $e = [System.Windows.Automation.AutomationElement]::FromHandle($WindowHandle)
    if ($null -eq $e) {
        return [ordered]@{
            runtime_id = ''
            name = ''
            automation_id = ''
            class_name = ''
            control_type = ''
            enabled = $null
            offscreen = $null
            is_keyboard_focusable = $null
            native_window_handle = $WindowHandle.ToInt64()
        }
    }
    $c = $e.Current
    $rid = try { $e.GetRuntimeId() -join '.' } catch { '' }
    return [ordered]@{
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

function Send-TargetTab {
    param(
        [uint32]$TargetThread,
        [bool]$Reverse
    )
    $currentThread = [KeyboardNative]::GetCurrentThreadId()
    $attached = $false
    try {
        if ($TargetThread -ne $currentThread) {
            $attached = [KeyboardNative]::AttachThreadInput($currentThread, $TargetThread, $true)
            if (-not $attached) { throw 'AttachThreadInput failed while posting hosted Tab input.' }
        }

        $keys = New-Object byte[] 256
        if (-not [KeyboardNative]::GetKeyboardState($keys)) { throw 'GetKeyboardState failed.' }
        $keys[[KeyboardNative]::VK_SHIFT] = $(if ($Reverse) { 0x80 } else { 0x00 })
        if (-not [KeyboardNative]::SetKeyboardState($keys)) { throw 'SetKeyboardState failed.' }

        $focus = Get-ThreadFocusHwnd $TargetThread
        if ($focus -eq [IntPtr]::Zero) { throw 'Target GUI thread has no keyboard focus before Tab.' }
        if (-not [KeyboardNative]::PostMessageW($focus, [KeyboardNative]::WM_KEYDOWN,
                [IntPtr][KeyboardNative]::VK_TAB, [IntPtr]1)) {
            throw 'Failed to post WM_KEYDOWN/VK_TAB to target focus window.'
        }
        if (-not [KeyboardNative]::PostMessageW($focus, [KeyboardNative]::WM_KEYUP,
                [IntPtr][KeyboardNative]::VK_TAB, [IntPtr]0xC0000001)) {
            throw 'Failed to post WM_KEYUP/VK_TAB to target focus window.'
        }
        Start-Sleep -Milliseconds 300

        # Restore shift state while queues are still attached.
        $keys[[KeyboardNative]::VK_SHIFT] = 0x00
        [KeyboardNative]::SetKeyboardState($keys) | Out-Null
    }
    finally {
        if ($attached) {
            [KeyboardNative]::AttachThreadInput($currentThread, $TargetThread, $false) | Out-Null
        }
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
        Start-Sleep -Milliseconds 150
    }
    finally {
        if ($attached) {
            [KeyboardNative]::AttachThreadInput($currentThread, $targetThread, $false) | Out-Null
        }
    }

    $initialHwnd = Get-ThreadFocusHwnd $targetThread
    if ($initialHwnd -ne $connectHwnd) {
        throw ('Could not establish Connect as target-thread focus; target focus=0x{0:X}, Connect=0x{1:X}.' -f $initialHwnd.ToInt64(), $connectHwnd.ToInt64())
    }
    $initial = Get-ElementRecordFromHwnd $initialHwnd

    Send-TargetTab -TargetThread $targetThread -Reverse $false
    $forwardHwnd = Get-ThreadFocusHwnd $targetThread
    $forward = Get-ElementRecordFromHwnd $forwardHwnd
    if ($forwardHwnd -eq [IntPtr]::Zero) { throw 'Target GUI thread has no focused element after Tab.' }
    if ($forwardHwnd -eq $initialHwnd) { throw 'Tab did not advance target-thread keyboard focus from Connect.' }

    Send-TargetTab -TargetThread $targetThread -Reverse $true
    $reverseHwnd = Get-ThreadFocusHwnd $targetThread
    $reverse = Get-ElementRecordFromHwnd $reverseHwnd
    if ($reverseHwnd -eq [IntPtr]::Zero) { throw 'Target GUI thread has no focused element after Shift+Tab.' }
    if ($reverseHwnd -ne $initialHwnd) {
        throw "Shift+Tab did not return target-thread focus to Connect; observed '$($reverse.name)' ($($reverse.control_type))."
    }

    $evidence = [ordered]@{
        schema_version = 2
        timestamp_utc = [DateTime]::UtcNow.ToString('o')
        gui_path = $gui
        process_id = $process.Id
        main_window_handle = ('0x{0:X}' -f $hwnd.ToInt64())
        gui_thread_id = $targetThread
        connect_native_style = ('0x{0:X}' -f $connectStyle)
        connect_native_tabstop = $nativeTabStop
        connect_uia_keyboard_focusable = $connect.Current.IsKeyboardFocusable
        focus_observation = 'GetGUIThreadInfo(target GUI thread)'
        input_delivery = 'PostMessage WM_KEYDOWN/WM_KEYUP VK_TAB with attached target keyboard state'
        initial_focus = $initial
        after_tab = $forward
        after_shift_tab = $reverse
        status = 'PASS'
        boundary = 'Hosted-native keyboard traversal evidence only; independent of unrelated foreground windows; not controlled interactive WinBot/native-desktop acceptance.'
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
