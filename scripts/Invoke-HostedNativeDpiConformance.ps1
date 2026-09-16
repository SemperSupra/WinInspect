param(
    [Parameter(Mandatory = $true)][string]$GuiPath,
    [string]$OutputPath = 'evidence/dpi-conformance.json',
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
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class DpiNative {
    public const UInt32 WM_DPICHANGED = 0x02E0;
    public const UInt32 SMTO_ABORTIFHUNG = 0x0002;

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern UInt32 GetDpiForWindow(IntPtr hwnd);

    [DllImport("user32.dll", SetLastError = true)]
    static extern IntPtr SendMessageTimeout(IntPtr hwnd, UInt32 msg, UIntPtr wParam, IntPtr lParam,
                                             UInt32 flags, UInt32 timeout, out UIntPtr result);

    public static void SendDpiChanged(IntPtr hwnd, UInt32 dpi, RECT suggested) {
        IntPtr p = Marshal.AllocHGlobal(Marshal.SizeOf<RECT>());
        try {
            Marshal.StructureToPtr(suggested, p, false);
            UInt64 packed = ((UInt64)dpi << 16) | dpi;
            UIntPtr result;
            IntPtr ok = SendMessageTimeout(hwnd, WM_DPICHANGED, new UIntPtr(packed), p,
                                           SMTO_ABORTIFHUNG, 5000, out result);
            if (ok == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "WM_DPICHANGED SendMessageTimeout failed");
        } finally { Marshal.FreeHGlobal(p); }
    }
}
'@

function Find-ByName($Root,[string]$Name) {
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::NameProperty, $Name)
    $e = $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
    if ($null -eq $e) { throw "Could not find '$Name' through UI Automation." }
    return $e
}

function Get-Geometry($Root) {
    $connect = Find-ByName $Root 'Connect'
    $refresh = Find-ByName $Root 'Refresh'
    $dashboard = Find-ByName $Root 'Dashboard'
    $cb = $connect.Current.BoundingRectangle
    $rb = $refresh.Current.BoundingRectangle
    $db = $dashboard.Current.BoundingRectangle
    [ordered]@{
        connect_width = [Math]::Round($cb.Width,2)
        connect_height = [Math]::Round($cb.Height,2)
        refresh_width = [Math]::Round($rb.Width,2)
        refresh_height = [Math]::Round($rb.Height,2)
        dashboard_width = [Math]::Round($db.Width,2)
        dashboard_height = [Math]::Round($db.Height,2)
    }
}

function Near([double]$Actual,[double]$Expected,[double]$Tolerance=2.0) {
    return [Math]::Abs($Actual-$Expected) -le $Tolerance
}

$process = Start-Process -FilePath $gui -PassThru
$evidence = [ordered]@{
    schema = 'wininspect.hosted-native-dpi-conformance.v1'
    timestamp_utc = [DateTime]::UtcNow.ToString('o')
    sequence = @()
    boundary = 'Deterministic WM_DPICHANGED/layout conformance on hosted native Windows; not a physical multi-monitor acceptance test.'
}
try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $hwnd = [IntPtr]::Zero
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        $hwnd = $process.MainWindowHandle
    } while ($hwnd -eq [IntPtr]::Zero -and -not $process.HasExited -and [DateTime]::UtcNow -lt $deadline)
    if ($process.HasExited) { throw "GUI exited before DPI probe with code $($process.ExitCode)." }
    if ($hwnd -eq [IntPtr]::Zero) { throw 'GUI did not expose a main window for DPI probe.' }

    $root = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
    if ($null -eq $root) { throw 'UI Automation could not bind to GUI root for DPI probe.' }
    $reportedInitialDpi = [DpiNative]::GetDpiForWindow($hwnd)
    $evidence.reported_initial_dpi = $reportedInitialDpi

    # Exercise an up-scale, a second up-scale, then a return to baseline. This catches
    # cumulative scaling, failure to scale top-toolbar controls, and non-reversible layout.
    $syntheticCurrentDpi = 96
    foreach ($targetDpi in @(96,144,192,96)) {
        if ($targetDpi -ne 96 -or $evidence.sequence.Count -gt 0) {
            $wr = New-Object DpiNative+RECT
            if (-not [DpiNative]::GetWindowRect($hwnd,[ref]$wr)) { throw 'GetWindowRect failed during DPI probe.' }
            $factor = [double]$targetDpi / [double]$syntheticCurrentDpi
            $w = [Math]::Max(400,[int][Math]::Round(($wr.Right-$wr.Left)*$factor))
            $h = [Math]::Max(300,[int][Math]::Round(($wr.Bottom-$wr.Top)*$factor))
            $suggested = New-Object DpiNative+RECT
            $suggested.Left = $wr.Left; $suggested.Top = $wr.Top
            $suggested.Right = $wr.Left + $w; $suggested.Bottom = $wr.Top + $h
            [DpiNative]::SendDpiChanged($hwnd,[uint32]$targetDpi,$suggested)
            Start-Sleep -Milliseconds 250
            $syntheticCurrentDpi = $targetDpi
        }

        $g = Get-Geometry $root
        $scale = [double]$targetDpi / 96.0
        $expected = [ordered]@{
            connect_width = 70*$scale
            connect_height = 26*$scale
            refresh_width = 80*$scale
            refresh_height = 24*$scale
            dashboard_width = 198*$scale
            dashboard_height = 32*$scale
        }
        $checks = [ordered]@{}
        foreach ($name in $expected.Keys) {
            $checks[$name] = Near ([double]$g[$name]) ([double]$expected[$name])
        }
        $all = -not ($checks.Values -contains $false)
        $evidence.sequence += [ordered]@{
            requested_dpi = $targetDpi
            geometry = $g
            expected = $expected
            checks = $checks
            pass = $all
        }
    }

    $evidence.status = if (-not ($evidence.sequence.pass -contains $false)) { 'PASS' } else { 'FAIL' }
}
finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
    $process.Dispose()
    $evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outputFull -Encoding utf8
}

Get-Content -LiteralPath $outputFull
if ($evidence.status -ne 'PASS') { throw 'Hosted-native DPI conformance failed.' }
