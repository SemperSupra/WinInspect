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
    public const UInt32 BM_CLICK = 0x00F5;

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern UInt32 GetDpiForWindow(IntPtr hwnd);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr GetDlgItem(IntPtr parent, int id);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageW(IntPtr hwnd, UInt32 msg, IntPtr wParam, IntPtr lParam);

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

function Find-ByAutomationId($Root,[string]$AutomationId) {
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $AutomationId)
    $e = $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
    if ($null -eq $e) { throw "Could not find AutomationId '$AutomationId' through UI Automation." }
    return $e
}

function Get-BoundsRecord($Element) {
    $b = $Element.Current.BoundingRectangle
    [ordered]@{ width=[Math]::Round($b.Width,2); height=[Math]::Round($b.Height,2) }
}

function Select-Tab([IntPtr]$MainWindow,[int]$TabId) {
    $button = [DpiNative]::GetDlgItem($MainWindow,$TabId)
    if ($button -eq [IntPtr]::Zero) { throw "Could not find sidebar tab ID $TabId." }
    [DpiNative]::SendMessageW($button,[DpiNative]::BM_CLICK,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 120
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

function Get-PanelGeometry($Root,[IntPtr]$MainWindow) {
    $result = [ordered]@{}
    $cases = @(
        @{ key='dashboard_recent_events'; tab=1000; kind='name'; value='Recent Events'; w=200; h=24 },
        @{ key='capture_full_screen'; tab=1002; kind='name'; value='Capture Full Screen'; w=130; h=28 },
        @{ key='input_left_click'; tab=1003; kind='name'; value='Left Click'; w=100; h=28 },
        @{ key='sessions_start_recording'; tab=1004; kind='name'; value='⏺ Start Recording'; w=130; h=28 },
        @{ key='events_log'; tab=1005; kind='id'; value='503'; w=540; h=350 },
        @{ key='metrics_method_breakdown'; tab=1006; kind='name'; value='Method Breakdown'; w=200; h=20 },
        @{ key='processes_kill'; tab=1007; kind='name'; value='Kill Process'; w=100; h=24 }
    )
    foreach ($case in $cases) {
        Select-Tab $MainWindow $case.tab
        $element = if ($case.kind -eq 'name') { Find-ByName $Root $case.value } else { Find-ByAutomationId $Root $case.value }
        $bounds = Get-BoundsRecord $element
        $result[$case.key] = [ordered]@{
            actual_width = $bounds.width
            actual_height = $bounds.height
            base_width = $case.w
            base_height = $case.h
        }
    }
    Select-Tab $MainWindow 1001
    return $result
}

function Near([double]$Actual,[double]$Expected,[double]$Tolerance=2.0) {
    return [Math]::Abs($Actual-$Expected) -le $Tolerance
}

$process = Start-Process -FilePath $gui -PassThru
$evidence = [ordered]@{
    schema = 'wininspect.hosted-native-dpi-conformance.v2'
    timestamp_utc = [DateTime]::UtcNow.ToString('o')
    sequence = @()
    boundary = 'Deterministic WM_DPICHANGED/layout conformance across all realized GUI panels on hosted native Windows; not a physical multi-monitor acceptance test.'
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

        Select-Tab $hwnd 1001
        $g = Get-Geometry $root
        $panelGeometry = Get-PanelGeometry $root $hwnd
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
        $panelChecks = [ordered]@{}
        foreach ($key in $panelGeometry.Keys) {
            $p = $panelGeometry[$key]
            $expectedWidth = [double]$p.base_width*$scale
            $expectedHeight = [double]$p.base_height*$scale
            $panelChecks[$key] = [ordered]@{
                width = Near ([double]$p.actual_width) $expectedWidth
                height = Near ([double]$p.actual_height) $expectedHeight
                expected_width = $expectedWidth
                expected_height = $expectedHeight
            }
        }
        $allPrimary = -not ($checks.Values -contains $false)
        $allPanels = -not (@($panelChecks.Values | ForEach-Object { $_.width -and $_.height }) -contains $false)
        $evidence.sequence += [ordered]@{
            requested_dpi = $targetDpi
            geometry = $g
            expected = $expected
            checks = $checks
            panel_geometry = $panelGeometry
            panel_checks = $panelChecks
            pass = ($allPrimary -and $allPanels)
        }
    }

    $evidence.status = if (-not ($evidence.sequence.pass -contains $false)) { 'PASS' } else { 'FAIL' }
}
finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
    $process.Dispose()
    $evidence | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $outputFull -Encoding utf8
}

Get-Content -LiteralPath $outputFull
if ($evidence.status -ne 'PASS') { throw 'Hosted-native DPI conformance failed.' }
