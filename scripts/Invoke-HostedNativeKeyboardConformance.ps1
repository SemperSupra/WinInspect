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
    public const uint BM_CLICK = 0x00F5;
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
    public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

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

function Find-VisibleByAutomationId($Root,[string]$AutomationId) {
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $AutomationId)
    $elements = $Root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)
    foreach ($element in $elements) {
        try {
            if (-not $element.Current.IsOffscreen -and $element.Current.NativeWindowHandle -ne 0) {
                return $element
            }
        } catch { }
    }
    throw "No visible native UIA element with AutomationId '$AutomationId' was found."
}

function Select-Tab([IntPtr]$MainWindow,[int]$TabId) {
    $button = [KeyboardNative]::GetDlgItem($MainWindow,$TabId)
    if ($button -eq [IntPtr]::Zero) { throw "Sidebar tab ID $TabId was not found." }
    [KeyboardNative]::SendMessageW($button,[KeyboardNative]::BM_CLICK,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 200
}

function Set-TargetFocus([IntPtr]$Target,[uint32]$TargetThread) {
    $currentThread = [KeyboardNative]::GetCurrentThreadId()
    $attached = $false
    try {
        if ($TargetThread -ne $currentThread) {
            $attached = [KeyboardNative]::AttachThreadInput($currentThread,$TargetThread,$true)
            if (-not $attached) { throw 'AttachThreadInput failed while establishing hosted keyboard focus.' }
        }
        [KeyboardNative]::SetFocus($Target) | Out-Null
        Start-Sleep -Milliseconds 120
    }
    finally {
        if ($attached) {
            [KeyboardNative]::AttachThreadInput($currentThread,$TargetThread,$false) | Out-Null
        }
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
        Start-Sleep -Milliseconds 250

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

    $cases = @(
        [ordered]@{ tab_id=1000; tab_name='Dashboard'; anchor_id='201'; anchor_label='Recent events list' },
        [ordered]@{ tab_id=1001; tab_name='Windows';   anchor_id='201'; anchor_label='Refresh' },
        [ordered]@{ tab_id=1002; tab_name='Capture';   anchor_id='301'; anchor_label='Capture Full Screen' },
        [ordered]@{ tab_id=1003; tab_name='Input';     anchor_id='401'; anchor_label='Mouse direction' },
        [ordered]@{ tab_id=1004; tab_name='Sessions';  anchor_id='501'; anchor_label='Start Recording' },
        [ordered]@{ tab_id=1005; tab_name='Events';    anchor_id='503'; anchor_label='Event log' },
        [ordered]@{ tab_id=1006; tab_name='Metrics';   anchor_id='504'; anchor_label='Method metrics list' },
        [ordered]@{ tab_id=1007; tab_name='Processes'; anchor_id='505'; anchor_label='Processes list' }
    )

    $caseResults = @()
    [uint32]$targetPid = 0
    $targetThread = [KeyboardNative]::GetWindowThreadProcessId($hwnd,[ref]$targetPid)
    if ($targetThread -eq 0) { throw 'Could not resolve the GUI thread.' }

    foreach ($case in $cases) {
        Select-Tab $hwnd $case.tab_id
        $anchor = Find-VisibleByAutomationId $root $case.anchor_id
        $anchorHwnd = [IntPtr]::new([int64]$anchor.Current.NativeWindowHandle)
        $anchorStyle = [KeyboardNative]::GetWindowLongPtr($anchorHwnd,[KeyboardNative]::GWL_STYLE).ToInt64()
        $nativeTabStop = (($anchorStyle -band [KeyboardNative]::WS_TABSTOP) -ne 0)
        if (-not $nativeTabStop) { throw "$($case.tab_name) anchor '$($case.anchor_label)' lacks WS_TABSTOP." }
        if (-not $anchor.Current.IsKeyboardFocusable) { throw "$($case.tab_name) anchor '$($case.anchor_label)' is not UIA keyboard-focusable." }

        Set-TargetFocus $anchorHwnd $targetThread
        $initialHwnd = Get-ThreadFocusHwnd $targetThread
        if ($initialHwnd -ne $anchorHwnd) {
            throw "$($case.tab_name) could not establish focus on '$($case.anchor_label)'."
        }
        $initial = Get-ElementRecordFromHwnd $initialHwnd

        Send-TargetTab -TargetThread $targetThread -Reverse $false
        $forwardHwnd = Get-ThreadFocusHwnd $targetThread
        if ($forwardHwnd -eq [IntPtr]::Zero -or $forwardHwnd -eq $initialHwnd) {
            throw "$($case.tab_name) Tab did not advance focus from '$($case.anchor_label)'."
        }
        $forward = Get-ElementRecordFromHwnd $forwardHwnd
        if ($forward.offscreen -eq $true) {
            throw "$($case.tab_name) Tab advanced into an offscreen/hidden control."
        }

        Send-TargetTab -TargetThread $targetThread -Reverse $true
        $reverseHwnd = Get-ThreadFocusHwnd $targetThread
        $reverse = Get-ElementRecordFromHwnd $reverseHwnd
        if ($reverseHwnd -ne $initialHwnd) {
            throw "$($case.tab_name) Shift+Tab did not return focus to '$($case.anchor_label)'; observed '$($reverse.name)' ($($reverse.control_type))."
        }

        $caseResults += [ordered]@{
            tab_id = $case.tab_id
            tab_name = $case.tab_name
            anchor_id = $case.anchor_id
            anchor_label = $case.anchor_label
            anchor_native_tabstop = $nativeTabStop
            anchor_uia_keyboard_focusable = $anchor.Current.IsKeyboardFocusable
            initial_focus = $initial
            after_tab = $forward
            after_shift_tab = $reverse
            status = 'PASS'
        }
    }

    $evidence = [ordered]@{
        schema_version = 3
        timestamp_utc = [DateTime]::UtcNow.ToString('o')
        gui_path = $gui
        process_id = $process.Id
        main_window_handle = ('0x{0:X}' -f $hwnd.ToInt64())
        gui_thread_id = $targetThread
        tab_count = $cases.Count
        tabs = $caseResults
        focus_observation = 'GetGUIThreadInfo(target GUI thread)'
        input_delivery = 'PostMessage WM_KEYDOWN/WM_KEYUP VK_TAB with attached target keyboard state'
        status = if ($caseResults.Count -eq $cases.Count -and -not ($caseResults.status -contains 'FAIL')) { 'PASS' } else { 'FAIL' }
        boundary = 'Hosted-native keyboard traversal across every realized GUI panel; independent of unrelated foreground windows; not controlled interactive WinBot/native-desktop acceptance.'
    }
    $evidence | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outputFull -Encoding utf8
    Get-Content -LiteralPath $outputFull
    if ($evidence.status -ne 'PASS') { throw 'All-panel keyboard conformance failed.' }
}
finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    $process.Dispose()
}
