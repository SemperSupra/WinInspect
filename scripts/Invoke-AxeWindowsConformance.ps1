param(
  [Parameter(Mandatory = $true)][string]$GuiPath,
  [string]$OutputDirectory = 'evidence/axe-windows'
)

$ErrorActionPreference = 'Stop'
$gui = (Resolve-Path $GuiPath).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null

$work = Join-Path $env:RUNNER_TEMP 'wininspect-axe-windows'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force -Path $work | Out-Null

Push-Location $work
try {
  dotnet new console --framework net8.0 --force | Out-Host
  if ($LASTEXITCODE -ne 0) { throw 'Unable to create Axe.Windows scanner host.' }

  dotnet add package Axe.Windows --version 2.4.2 --source https://api.nuget.org/v3/index.json | Out-Host
  if ($LASTEXITCODE -ne 0) { throw 'Unable to restore pinned Axe.Windows 2.4.2 package.' }

  $program = @'
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using Axe.Windows.Automation;

if (args.Length != 2)
{
    Console.Error.WriteLine("usage: scanner <gui-path> <output-directory>");
    return 64;
}

var guiPath = Path.GetFullPath(args[0]);
var outputDirectory = Path.GetFullPath(args[1]);
Directory.CreateDirectory(outputDirectory);

using var process = Process.Start(new ProcessStartInfo
{
    FileName = guiPath,
    UseShellExecute = false,
    WorkingDirectory = Path.GetDirectoryName(guiPath)!
}) ?? throw new InvalidOperationException("Failed to start WinInspect GUI.");

try
{
    var deadline = DateTime.UtcNow.AddSeconds(15);
    while (DateTime.UtcNow < deadline && !process.HasExited)
    {
        process.Refresh();
        if (process.MainWindowHandle != IntPtr.Zero)
            break;
        Thread.Sleep(100);
    }

    if (process.HasExited)
        throw new InvalidOperationException($"WinInspect GUI exited before scan with code {process.ExitCode}.");
    if (process.MainWindowHandle == IntPtr.Zero)
        throw new TimeoutException("WinInspect GUI did not expose a top-level window within 15 seconds.");

    // Let initial disconnected-state UI settle before scanning the process UIA tree.
    Thread.Sleep(750);

    var config = Config.Builder.ForProcessId(process.Id)
        .WithOutputFileFormat(OutputFileFormat.A11yTest)
        .WithOutputDirectory(outputDirectory)
        .Build();
    var scanner = ScannerFactory.CreateScanner(config);
    var result = scanner.Scan(new ScanOptions(scanId: "wininspect-gui"));

    var windows = result.WindowScanOutputs.ToArray();
    var totalErrors = windows.Sum(w => w.ErrorCount);
    var summary = new
    {
        process_id = process.Id,
        main_window_handle = process.MainWindowHandle.ToInt64(),
        window_count = windows.Length,
        error_count = totalErrors,
        axe_windows_version = "2.4.2",
        timestamp_utc = DateTime.UtcNow.ToString("O")
    };

    File.WriteAllText(
        Path.Combine(outputDirectory, "summary.json"),
        JsonSerializer.Serialize(summary, new JsonSerializerOptions { WriteIndented = true }));

    Console.WriteLine($"Axe.Windows scanned {windows.Length} top-level window(s); errors={totalErrors}.");
    return totalErrors == 0 ? 0 : 2;
}
finally
{
    if (!process.HasExited)
    {
        try { process.Kill(entireProcessTree: true); } catch { }
        try { process.WaitForExit(5000); } catch { }
    }
}
'@

  [IO.File]::WriteAllText((Join-Path $work 'Program.cs'), $program, [Text.UTF8Encoding]::new($false))

  dotnet run --configuration Release -- $gui $output | Out-Host
  $scannerExit = $LASTEXITCODE
  if ($scannerExit -eq 2) {
    throw 'Axe.Windows reported one or more accessibility-rule errors.'
  }
  if ($scannerExit -ne 0) {
    throw "Axe.Windows scanner host failed with exit code $scannerExit."
  }
}
finally {
  Pop-Location
}

if (-not (Test-Path (Join-Path $output 'summary.json'))) {
  throw 'Axe.Windows scan did not produce a summary artifact.'
}
Get-Content (Join-Path $output 'summary.json')
