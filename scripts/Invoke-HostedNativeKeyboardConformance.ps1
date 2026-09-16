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
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO {
        public uint cbSize; public uint flags; public IntPtr hwndActive; public IntPtr hwndFocus;
        public IntPtr hwndCapture; public IntPtr hwndMenuOwner; public IntPtr hwndMoveSize;
        public IntPtr hwndCaret; public RECT rcCaret;
    }
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW", SetLastError = true)] public static extern IntPtr GetWindowLongPtr(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll", SetLastError = true)] public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", SetLastError = true)] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll", SetLastError = true)][return: MarshalAs(UnmanagedType.Bool)] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
    [DllImport("user32.dll", SetLastError = true)] public static extern IntPtr SetFocus(IntPtr hWnd);
    [DllImport("user32.dll", SetLastError = true)][return: MarshalAs(UnmanagedType.Bool)] public static extern bool GetGUIThreadInfo(uint idThread, ref GUITHREADINFO info);
    [DllImport("user32.dll", SetLastError = true)][return: MarshalAs(UnmanagedType.Bool)] public static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", SetLastError = true)][return: MarshalAs(UnmanagedType.Bool)] public static extern bool GetKeyboardState([Out] byte[] state);
    [DllImport("user32.dll", SetLastError = true)][return: MarshalAs(UnmanagedType.Bool)] public static extern bool SetKeyboardState(byte[] state);
}
'@

function Get-ThreadFocusHwnd([uint32]$ThreadId) {
    $info = [KeyboardNative+GUITHREADINFO]::new()
    $info.cbSize = [Runtime.InteropServices.Marshal]::SizeOf([type][KeyboardNative+GUITHREADINFO])
    if (-not [KeyboardNative]::GetGUIThreadInfo($ThreadId,[ref]$info)) { throw "GetGUIThreadInfo failed for GUI thread $ThreadId." }
    return $info.hwndFocus
}

function Get-ElementRecord($Element) {
    if ($null -eq $Element) { return $null }
    $c = $Element.Current
    $rid = try { $Element.GetRuntimeId() -join '.' } catch { '' }
    return [ordered]@{
        runtime_id=$rid; name=$c.Name; automation_id=$c.AutomationId; class_name=$c.ClassName
        control_type=$c.ControlType.ProgrammaticName; enabled=$c.IsEnabled; offscreen=$c.IsOffscreen
        is_keyboard_focusable=$c.IsKeyboardFocusable; native_window_handle=$c.NativeWindowHandle
    }
}

function Get-ElementRecordFromHwnd([IntPtr]$WindowHandle) {
    if ($WindowHandle -eq [IntPtr]::Zero) { return $null }
    return Get-ElementRecord ([System.Windows.Automation.AutomationElement]::FromHandle($WindowHandle))
}

function Find-VisibleControlPair($Root,[string]$AutomationId) {
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::AutomationIdProperty,$AutomationId)
    $elements = $Root.FindAll([System.Windows.Automation.TreeScope]::Descendants,$condition)
    $visible = @()
    foreach ($element in $elements) {
        try { if (-not $element.Current.IsOffscreen) { $visible += $element } } catch { }
    }
    if ($visible.Count -eq 0) { throw "No visible UIA element with AutomationId '$AutomationId' was found." }

    $native = $visible | Where-Object { $_.Current.NativeWindowHandle -ne 0 } | Select-Object -First 1
    if ($null -eq $native) { throw "No visible native HWND proxy with AutomationId '$AutomationId' was found." }
    $semantic = $visible | Where-Object { $_.Current.ControlType.ProgrammaticName -ne 'ControlType.Pane' } | Select-Object -First 1
    if ($null -eq $semantic) { $semantic = $native }

    return [ordered]@{
        native = $native
        semantic = $semantic
        candidates = @($visible | ForEach-Object { Get-ElementRecord $_ })
    }
}

function Select-Tab([IntPtr]$MainWindow,[int]$TabId) {
    $button=[KeyboardNative]::GetDlgItem($MainWindow,$TabId)
    if ($button -eq [IntPtr]::Zero) { throw "Sidebar tab ID $TabId was not found." }
    [KeyboardNative]::SendMessageW($button,[KeyboardNative]::BM_CLICK,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 200
}

function Set-TargetFocus([IntPtr]$Target,[uint32]$TargetThread) {
    $currentThread=[KeyboardNative]::GetCurrentThreadId(); $attached=$false
    try {
        if ($TargetThread -ne $currentThread) {
            $attached=[KeyboardNative]::AttachThreadInput($currentThread,$TargetThread,$true)
            if (-not $attached) { throw 'AttachThreadInput failed while establishing hosted keyboard focus.' }
        }
        [KeyboardNative]::SetFocus($Target) | Out-Null
        Start-Sleep -Milliseconds 120
    } finally { if ($attached) { [KeyboardNative]::AttachThreadInput($currentThread,$TargetThread,$false) | Out-Null } }
}

function Send-TargetTab([uint32]$TargetThread,[bool]$Reverse) {
    $currentThread=[KeyboardNative]::GetCurrentThreadId(); $attached=$false
    try {
        if ($TargetThread -ne $currentThread) {
            $attached=[KeyboardNative]::AttachThreadInput($currentThread,$TargetThread,$true)
            if (-not $attached) { throw 'AttachThreadInput failed while posting hosted Tab input.' }
        }
        $keys=New-Object byte[] 256
        if (-not [KeyboardNative]::GetKeyboardState($keys)) { throw 'GetKeyboardState failed.' }
        $keys[[KeyboardNative]::VK_SHIFT]=$(if($Reverse){0x80}else{0x00})
        if (-not [KeyboardNative]::SetKeyboardState($keys)) { throw 'SetKeyboardState failed.' }
        $focus=Get-ThreadFocusHwnd $TargetThread
        if ($focus -eq [IntPtr]::Zero) { throw 'Target GUI thread has no keyboard focus before Tab.' }
        if (-not [KeyboardNative]::PostMessageW($focus,[KeyboardNative]::WM_KEYDOWN,[IntPtr][KeyboardNative]::VK_TAB,[IntPtr]1)) { throw 'Failed to post WM_KEYDOWN/VK_TAB.' }
        if (-not [KeyboardNative]::PostMessageW($focus,[KeyboardNative]::WM_KEYUP,[IntPtr][KeyboardNative]::VK_TAB,[IntPtr]0xC0000001)) { throw 'Failed to post WM_KEYUP/VK_TAB.' }
        Start-Sleep -Milliseconds 250
        $keys[[KeyboardNative]::VK_SHIFT]=0; [KeyboardNative]::SetKeyboardState($keys)|Out-Null
    } finally { if ($attached) { [KeyboardNative]::AttachThreadInput($currentThread,$TargetThread,$false)|Out-Null } }
}

$process=Start-Process -FilePath $gui -PassThru
$evidence=[ordered]@{
    schema_version=5; timestamp_utc=[DateTime]::UtcNow.ToString('o'); gui_path=$gui; tabs=@()
    boundary='Hosted-native keyboard traversal across every realized GUI panel with native HWND and semantic UIA evidence separated; not controlled interactive WinBot/native-desktop acceptance.'
}
try {
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds); $hwnd=[IntPtr]::Zero
    do { Start-Sleep -Milliseconds 100; $process.Refresh(); $hwnd=$process.MainWindowHandle }
    while($hwnd -eq [IntPtr]::Zero -and -not $process.HasExited -and [DateTime]::UtcNow -lt $deadline)
    if($process.HasExited){throw "GUI exited before keyboard probe with code $($process.ExitCode)."}
    if($hwnd -eq [IntPtr]::Zero){throw 'GUI did not expose a main window for keyboard probe.'}
    $root=[System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
    if($null -eq $root){throw 'UI Automation could not bind to the GUI root.'}

    $cases=@(
        [ordered]@{tab_id=1000;tab_name='Dashboard';anchor_id='201';anchor_label='Recent events list'},
        [ordered]@{tab_id=1001;tab_name='Windows';anchor_id='201';anchor_label='Refresh'},
        [ordered]@{tab_id=1002;tab_name='Capture';anchor_id='301';anchor_label='Capture Full Screen'},
        [ordered]@{tab_id=1003;tab_name='Input';anchor_id='401';anchor_label='Mouse direction'},
        [ordered]@{tab_id=1004;tab_name='Sessions';anchor_id='501';anchor_label='Start Recording'},
        [ordered]@{tab_id=1005;tab_name='Events';anchor_id='503';anchor_label='Event log'},
        [ordered]@{tab_id=1006;tab_name='Metrics';anchor_id='504';anchor_label='Method metrics list'},
        [ordered]@{tab_id=1007;tab_name='Processes';anchor_id='505';anchor_label='Processes list'}
    )
    [uint32]$targetPid=0; $targetThread=[KeyboardNative]::GetWindowThreadProcessId($hwnd,[ref]$targetPid)
    if($targetThread -eq 0){throw 'Could not resolve the GUI thread.'}
    $evidence.process_id=$process.Id; $evidence.main_window_handle=('0x{0:X}' -f $hwnd.ToInt64()); $evidence.gui_thread_id=$targetThread
    $evidence.tab_count=$cases.Count; $evidence.focus_observation='GetGUIThreadInfo(target GUI thread)'
    $evidence.input_delivery='PostMessage WM_KEYDOWN/WM_KEYUP VK_TAB with attached target keyboard state'

    foreach($case in $cases){
        $violations=[System.Collections.Generic.List[string]]::new()
        $result=[ordered]@{
            tab_id=$case.tab_id;tab_name=$case.tab_name;anchor_id=$case.anchor_id;anchor_label=$case.anchor_label
            uia_candidates=@();semantic_element=$null;anchor_native_tabstop=$null;semantic_uia_keyboard_focusable=$null
            initial_focus=$null;after_tab=$null;after_shift_tab=$null;focus_established=$false;tab_advanced=$false;shift_tab_returned=$false
            violations=@();status='FAIL'
        }
        try {
            Select-Tab $hwnd $case.tab_id
            $pair=Find-VisibleControlPair $root $case.anchor_id
            $result.uia_candidates=$pair.candidates
            $result.semantic_element=Get-ElementRecord $pair.semantic
            $nativeHwnd=[IntPtr]::new([int64]$pair.native.Current.NativeWindowHandle)
            $style=[KeyboardNative]::GetWindowLongPtr($nativeHwnd,[KeyboardNative]::GWL_STYLE).ToInt64()
            $result.anchor_native_tabstop=(($style -band [KeyboardNative]::WS_TABSTOP)-ne 0)
            $result.semantic_uia_keyboard_focusable=$pair.semantic.Current.IsKeyboardFocusable
            if(-not $result.anchor_native_tabstop){$violations.Add('missing_ws_tabstop')}
            if(-not $result.semantic_uia_keyboard_focusable){$violations.Add('semantic_uia_not_keyboard_focusable')}

            Set-TargetFocus $nativeHwnd $targetThread
            $initialHwnd=Get-ThreadFocusHwnd $targetThread; $result.initial_focus=Get-ElementRecordFromHwnd $initialHwnd
            $result.focus_established=($initialHwnd -eq $nativeHwnd)
            if(-not $result.focus_established){$violations.Add('native_focus_not_established')}
            if($result.focus_established){
                Send-TargetTab $targetThread $false
                $forwardHwnd=Get-ThreadFocusHwnd $targetThread; $result.after_tab=Get-ElementRecordFromHwnd $forwardHwnd
                $result.tab_advanced=($forwardHwnd -ne [IntPtr]::Zero -and $forwardHwnd -ne $initialHwnd)
                if(-not $result.tab_advanced){$violations.Add('tab_did_not_advance')}
                elseif($result.after_tab.offscreen -eq $true){$violations.Add('tab_advanced_to_hidden_control')}
                if($result.tab_advanced){
                    Send-TargetTab $targetThread $true
                    $reverseHwnd=Get-ThreadFocusHwnd $targetThread; $result.after_shift_tab=Get-ElementRecordFromHwnd $reverseHwnd
                    $result.shift_tab_returned=($reverseHwnd -eq $initialHwnd)
                    if(-not $result.shift_tab_returned){$violations.Add('shift_tab_did_not_return')}
                }
            }
        } catch { $violations.Add(('exception: '+$_.Exception.Message)) }
        $result.violations=@($violations); $result.status=if($violations.Count -eq 0){'PASS'}else{'FAIL'}
        $evidence.tabs+=$result
        Write-Host ("KEYBOARD_TAB tab={0} status={1} native_tabstop={2} semantic_type={3} semantic_focusable={4} focus={5} tab={6} reverse={7} violations={8}" -f
            $result.tab_name,$result.status,$result.anchor_native_tabstop,$result.semantic_element.control_type,$result.semantic_uia_keyboard_focusable,
            $result.focus_established,$result.tab_advanced,$result.shift_tab_returned,($result.violations -join ','))
    }
    $evidence.status=if(-not($evidence.tabs.status -contains 'FAIL')){'PASS'}else{'FAIL'}
}
finally{
    if(-not $process.HasExited){Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue}
    $process.Dispose(); $evidence|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $outputFull -Encoding utf8
}
Get-Content -LiteralPath $outputFull
if($evidence.status -ne 'PASS'){throw 'All-panel keyboard conformance failed; inspect native and semantic per-tab evidence.'}
