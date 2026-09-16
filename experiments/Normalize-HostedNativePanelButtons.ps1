$ErrorActionPreference = 'Stop'
$path = 'source/clients/gui/src/gui_main.cpp'
$text = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")
$lines = $text -split "`n", -1
$needle = 'WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,'
$replacement = 'WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,'
$changed = 0

for ($i = 0; $i -lt $lines.Length; $i++) {
    if (-not $lines[$i].Contains($needle)) { continue }
    $end = [Math]::Min($lines.Length - 1, $i + 3)
    $window = ($lines[$i..$end] -join "`n")
    if ($window.Contains('hPanel,') -or $window.Contains('hWinPanel,')) {
        $lines[$i] = $lines[$i].Replace($needle, $replacement)
        $changed++
    }
}

# Source-level occurrences: Refresh, Highlight, three Capture buttons, mouse pad,
# two click buttons, two Send buttons, Start Recording, and Kill Process.
if ($changed -ne 12) {
    throw "Expected to normalize exactly 12 panel-owned owner-draw button sites; changed $changed."
}

[IO.File]::WriteAllText($path, ($lines -join "`n"), [Text.UTF8Encoding]::new($false))
Write-Host "Normalized $changed panel-owned action-button sites to native push-button rendering."
