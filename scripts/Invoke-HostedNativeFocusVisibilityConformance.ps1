param(
    [Parameter(Mandatory = $true)][string]$GuiPath,
    [string]$OutputPath = 'evidence/focus-visibility.json',
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
$gui = (Resolve-Path -LiteralPath $GuiPath).Path
$outputFull = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputFull) | Out-Null

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FocusVisualNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("kernel32.dll")]
    public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool AttachThreadInput(uint from, uint to, bool attach);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr SetFocus(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")]
    public static extern bool RedrawWindow(IntPtr hwnd, IntPtr updateRect, IntPtr updateRegion, uint flags);
}
'@

function Find-ByProperty($Root,$Property,$Value) {
    $condition = [System.Windows.Automation.PropertyCondition]::new($Property,$Value)
    $e = $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,$condition)
    if ($null -eq $e) { throw "Could not locate UIA element '$Value'." }
    return $e
}

function Set-TargetFocus([IntPtr]$Target,[uint32]$TargetThread) {
    $current = [FocusVisualNative]::GetCurrentThreadId()
    $attached = $false
    try {
        if ($current -ne $TargetThread) {
            $attached = [FocusVisualNative]::AttachThreadInput($current,$TargetThread,$true)
            if (-not $attached) { throw 'AttachThreadInput failed for focus-visibility probe.' }
        }
        [FocusVisualNative]::SetFocus($Target) | Out-Null
    } finally {
        if ($attached) { [FocusVisualNative]::AttachThreadInput($current,$TargetThread,$false) | Out-Null }
    }
}

function Capture-Window([IntPtr]$Hwnd) {
    $wr = New-Object FocusVisualNative+RECT
    if (-not [FocusVisualNative]::GetWindowRect($Hwnd,[ref]$wr)) { throw 'GetWindowRect failed.' }
    $w = $wr.Right-$wr.Left; $h=$wr.Bottom-$wr.Top
    $bmp = [System.Drawing.Bitmap]::new($w,$h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $hdc = $g.GetHdc()
        try {
            if (-not [FocusVisualNative]::PrintWindow($Hwnd,$hdc,2)) { throw 'PrintWindow failed.' }
        } finally { $g.ReleaseHdc($hdc) }
    } finally { $g.Dispose() }
    return [ordered]@{ bitmap=$bmp; rect=$wr }
}

$process = Start-Process -FilePath $gui -PassThru
$focusedCapture = $null
$unfocusedCapture = $null
$evidence = [ordered]@{
    schema = 'wininspect.hosted-native-focus-visibility.v1'
    timestamp_utc = [DateTime]::UtcNow.ToString('o')
    boundary = 'Hosted-native owner-draw keyboard-focus rendering evidence; not human visual inspection or controlled interactive WinBot acceptance.'
}
try {
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $hwnd=[IntPtr]::Zero
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh(); $hwnd=$process.MainWindowHandle
    } while ($hwnd -eq [IntPtr]::Zero -and -not $process.HasExited -and [DateTime]::UtcNow -lt $deadline)
    if ($process.HasExited -or $hwnd -eq [IntPtr]::Zero) { throw 'GUI unavailable for focus-visibility probe.' }

    $root=[System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
    $connect=Find-ByProperty $root ([System.Windows.Automation.AutomationElement]::NameProperty) 'Connect'
    $search=Find-ByProperty $root ([System.Windows.Automation.AutomationElement]::AutomationIdProperty) '602'
    $connectHwnd=[IntPtr]::new([int64]$connect.Current.NativeWindowHandle)
    $searchHwnd=[IntPtr]::new([int64]$search.Current.NativeWindowHandle)
    if ($connectHwnd -eq [IntPtr]::Zero -or $searchHwnd -eq [IntPtr]::Zero) { throw 'Focus targets lacked native HWNDs.' }
    [uint32]$pid=0
    $thread=[FocusVisualNative]::GetWindowThreadProcessId($connectHwnd,[ref]$pid)
    if ($thread -eq 0) { throw 'Could not resolve GUI thread.' }

    Set-TargetFocus $connectHwnd $thread
    [FocusVisualNative]::RedrawWindow($connectHwnd,[IntPtr]::Zero,[IntPtr]::Zero,0x0001 -bor 0x0100 -bor 0x0080) | Out-Null
    Start-Sleep -Milliseconds 200
    $focusedCapture=Capture-Window $hwnd

    Set-TargetFocus $searchHwnd $thread
    [FocusVisualNative]::RedrawWindow($connectHwnd,[IntPtr]::Zero,[IntPtr]::Zero,0x0001 -bor 0x0100 -bor 0x0080) | Out-Null
    Start-Sleep -Milliseconds 200
    $unfocusedCapture=Capture-Window $hwnd

    $cb=$connect.Current.BoundingRectangle
    $wr=$focusedCapture.rect
    $left=[Math]::Max(0,[int][Math]::Floor($cb.Left-$wr.Left))
    $top=[Math]::Max(0,[int][Math]::Floor($cb.Top-$wr.Top))
    $right=[Math]::Min($focusedCapture.bitmap.Width-1,[int][Math]::Ceiling($cb.Right-$wr.Left)-1)
    $bottom=[Math]::Min($focusedCapture.bitmap.Height-1,[int][Math]::Ceiling($cb.Bottom-$wr.Top)-1)
    if ($right -le $left -or $bottom -le $top) { throw 'Invalid Connect crop for focus visibility.' }

    $different=0; $pixels=0
    for ($y=$top;$y -le $bottom;$y++) {
        for ($x=$left;$x -le $right;$x++) {
            $a=$focusedCapture.bitmap.GetPixel($x,$y)
            $b=$unfocusedCapture.bitmap.GetPixel($x,$y)
            $pixels++
            if ($a.ToArgb() -ne $b.ToArgb()) { $different++ }
        }
    }
    $ratio=if($pixels){[double]$different/$pixels}else{0.0}
    $visible=($different -ge 8)
    $evidence.connect_pixels=$pixels
    $evidence.changed_pixels=$different
    $evidence.changed_ratio=$ratio
    $evidence.visible_focus_delta=$visible
    $evidence.status=if($visible){'PASS'}else{'FAIL'}
}
finally {
    if ($null -ne $focusedCapture) { $focusedCapture.bitmap.Dispose() }
    if ($null -ne $unfocusedCapture) { $unfocusedCapture.bitmap.Dispose() }
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
    $process.Dispose()
    $evidence | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $outputFull -Encoding utf8
}

Get-Content -LiteralPath $outputFull
if ($evidence.status -ne 'PASS') { throw 'Owner-draw keyboard focus produced no visible Connect-button delta.' }
