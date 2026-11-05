param(
    [Parameter(Mandatory = $true)][string]$GuiPath,
    [string]$OutputPath = 'evidence/high-contrast.json',
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
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class HighContrastNative {
    public const UInt32 SPI_GETHIGHCONTRAST = 0x0042;
    public const UInt32 SPI_SETHIGHCONTRAST = 0x0043;
    public const UInt32 SPIF_SENDCHANGE = 0x0002;
    public const UInt32 HCF_HIGHCONTRASTON = 0x00000001;
    public const int COLOR_WINDOW = 5;
    public const int COLOR_HIGHLIGHT = 13;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct HIGHCONTRAST {
        public UInt32 cbSize;
        public UInt32 dwFlags;
        public IntPtr lpszDefaultScheme;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool SystemParametersInfo(UInt32 action, UInt32 param, ref HIGHCONTRAST value, UInt32 flags);

    [DllImport("user32.dll")]
    public static extern UInt32 GetSysColor(int index);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, UInt32 flags);

    public static UInt32 GetFlags(out string scheme) {
        HIGHCONTRAST hc = new HIGHCONTRAST();
        hc.cbSize = (UInt32)Marshal.SizeOf<HIGHCONTRAST>();
        if (!SystemParametersInfo(SPI_GETHIGHCONTRAST, hc.cbSize, ref hc, 0))
            throw new Win32Exception(Marshal.GetLastWin32Error(), "SPI_GETHIGHCONTRAST failed");
        scheme = hc.lpszDefaultScheme == IntPtr.Zero ? null : Marshal.PtrToStringUni(hc.lpszDefaultScheme);
        return hc.dwFlags;
    }

    public static void SetFlags(UInt32 flags, string scheme) {
        IntPtr p = IntPtr.Zero;
        try {
            if (!String.IsNullOrEmpty(scheme)) p = Marshal.StringToHGlobalUni(scheme);
            HIGHCONTRAST hc = new HIGHCONTRAST();
            hc.cbSize = (UInt32)Marshal.SizeOf<HIGHCONTRAST>();
            hc.dwFlags = flags;
            hc.lpszDefaultScheme = p;
            if (!SystemParametersInfo(SPI_SETHIGHCONTRAST, hc.cbSize, ref hc, SPIF_SENDCHANGE))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "SPI_SETHIGHCONTRAST failed");
        } finally {
            if (p != IntPtr.Zero) Marshal.FreeHGlobal(p);
        }
    }
}
'@

function Convert-ColorRef([uint32]$ColorRef) {
    [ordered]@{
        r = [int]($ColorRef -band 0xff)
        g = [int](($ColorRef -shr 8) -band 0xff)
        b = [int](($ColorRef -shr 16) -band 0xff)
    }
}

function Color-Matches($Actual, $Expected) {
    return $Actual.R -eq $Expected.r -and $Actual.G -eq $Expected.g -and $Actual.B -eq $Expected.b
}

[string]$originalScheme = $null
$originalFlags = [HighContrastNative]::GetFlags([ref]$originalScheme)
$originalOn = (($originalFlags -band [HighContrastNative]::HCF_HIGHCONTRASTON) -ne 0)
$process = $null
$bitmap = $null
$changed = $false
$evidence = [ordered]@{
    schema = 'wininspect.hosted-native-high-contrast.v1'
    timestamp_utc = [DateTime]::UtcNow.ToString('o')
    original_high_contrast = $originalOn
    original_flags = ('0x{0:X8}' -f $originalFlags)
    restored = $false
    boundary = 'Hosted-native High Contrast rendering evidence only; not Narrator or controlled interactive WinBot acceptance.'
}

try {
    if (-not $originalOn) {
        [HighContrastNative]::SetFlags(($originalFlags -bor [HighContrastNative]::HCF_HIGHCONTRASTON), $originalScheme)
        $changed = $true
        Start-Sleep -Milliseconds 750
    }

    [string]$activeScheme = $null
    $activeFlags = [HighContrastNative]::GetFlags([ref]$activeScheme)
    $activeOn = (($activeFlags -band [HighContrastNative]::HCF_HIGHCONTRASTON) -ne 0)
    if (-not $activeOn) { throw 'Windows did not report High Contrast active after SPI_SETHIGHCONTRAST.' }

    $expectedWindow = Convert-ColorRef ([HighContrastNative]::GetSysColor([HighContrastNative]::COLOR_WINDOW))
    $expectedHighlight = Convert-ColorRef ([HighContrastNative]::GetSysColor([HighContrastNative]::COLOR_HIGHLIGHT))

    $process = Start-Process -FilePath $gui -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $hwnd = [IntPtr]::Zero
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        $hwnd = $process.MainWindowHandle
    } while ($hwnd -eq [IntPtr]::Zero -and -not $process.HasExited -and [DateTime]::UtcNow -lt $deadline)
    if ($process.HasExited) { throw "GUI exited during High Contrast probe with code $($process.ExitCode)." }
    if ($hwnd -eq [IntPtr]::Zero) { throw 'GUI did not expose a main window during High Contrast probe.' }

    $root = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
    if ($null -eq $root) { throw 'UI Automation could not bind to the High Contrast GUI root.' }
    function Find-ByName([string]$Name) {
        $condition = [System.Windows.Automation.PropertyCondition]::new(
            [System.Windows.Automation.AutomationElement]::NameProperty, $Name)
        return $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
    }
    $windowsTab = Find-ByName 'Windows'
    $dashboardTab = Find-ByName 'Dashboard'
    if ($null -eq $windowsTab -or $null -eq $dashboardTab) {
        throw 'Could not locate Windows/Dashboard sidebar controls for High Contrast pixel oracle.'
    }

    $wr = New-Object HighContrastNative+RECT
    if (-not [HighContrastNative]::GetWindowRect($hwnd, [ref]$wr)) { throw 'GetWindowRect failed for High Contrast probe.' }
    $width = $wr.Right - $wr.Left
    $height = $wr.Bottom - $wr.Top
    if ($width -le 0 -or $height -le 0) { throw "Invalid High Contrast window bounds ${width}x${height}." }

    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $hdc = $graphics.GetHdc()
        try {
            if (-not [HighContrastNative]::PrintWindow($hwnd, $hdc, 2)) { throw 'PrintWindow failed during High Contrast probe.' }
        } finally { $graphics.ReleaseHdc($hdc) }
    } finally { $graphics.Dispose() }

    function Sample-SidebarBackground($Element) {
        $r = $Element.Current.BoundingRectangle
        $x = [int][Math]::Round($r.Right - $wr.Left - 8)
        $y = [int][Math]::Round($r.Top - $wr.Top + ($r.Height / 2.0))
        if ($x -lt 0 -or $x -ge $bitmap.Width -or $y -lt 0 -or $y -ge $bitmap.Height) {
            throw "Sidebar sample coordinate ${x},${y} is outside captured window."
        }
        return [ordered]@{ x = $x; y = $y; color = $bitmap.GetPixel($x,$y) }
    }

    $activeSample = Sample-SidebarBackground $windowsTab
    $inactiveSample = Sample-SidebarBackground $dashboardTab
    $activeMatches = Color-Matches $activeSample.color $expectedHighlight
    $inactiveMatches = Color-Matches $inactiveSample.color $expectedWindow

    $evidence.active_flags = ('0x{0:X8}' -f $activeFlags)
    $evidence.high_contrast_active = $activeOn
    $evidence.system_window = $expectedWindow
    $evidence.system_highlight = $expectedHighlight
    $evidence.active_windows_tab = [ordered]@{
        x = $activeSample.x; y = $activeSample.y
        r = $activeSample.color.R; g = $activeSample.color.G; b = $activeSample.color.B
        matches_system_highlight = $activeMatches
    }
    $evidence.inactive_dashboard_tab = [ordered]@{
        x = $inactiveSample.x; y = $inactiveSample.y
        r = $inactiveSample.color.R; g = $inactiveSample.color.G; b = $inactiveSample.color.B
        matches_system_window = $inactiveMatches
    }
    $evidence.status = if ($activeMatches -and $inactiveMatches) { 'PASS' } else { 'FAIL' }
    if ($evidence.status -ne 'PASS') {
        throw 'High Contrast sidebar rendering did not match Windows system highlight/window colors.'
    }
}
finally {
    if ($null -ne $bitmap) { $bitmap.Dispose() }
    if ($null -ne $process) {
        try { if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } } catch {}
        $process.Dispose()
    }
    if ($changed) {
        try {
            [HighContrastNative]::SetFlags($originalFlags, $originalScheme)
            Start-Sleep -Milliseconds 500
            [string]$verifyScheme = $null
            $verifyFlags = [HighContrastNative]::GetFlags([ref]$verifyScheme)
            $evidence.restored = ($verifyFlags -eq $originalFlags)
            if (-not $evidence.restored) { throw 'Original High Contrast flags were not restored.' }
        } catch {
            $evidence.restore_error = $_.Exception.Message
        }
    } else {
        $evidence.restored = $true
    }
    $evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outputFull -Encoding utf8
}

Get-Content -LiteralPath $outputFull
if ($evidence.status -ne 'PASS') { throw 'Hosted-native High Contrast conformance failed.' }
if (-not $evidence.restored) { throw 'Hosted runner High Contrast state was not restored.' }
