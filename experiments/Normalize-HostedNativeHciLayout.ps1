$ErrorActionPreference = 'Stop'

function Replace-Exactly {
    param([string]$Path,[string]$Old,[string]$New)
    $text = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $oldNorm = $Old.Replace("`r`n", "`n")
    $newNorm = $New.Replace("`r`n", "`n")
    $first = $text.IndexOf($oldNorm, [StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Expected layout block not found in $Path" }
    if ($text.IndexOf($oldNorm, $first + $oldNorm.Length, [StringComparison]::Ordinal) -ge 0) { throw "Layout block is not unique in $Path" }
    $updated = $text.Substring(0,$first) + $newNorm + $text.Substring($first + $oldNorm.Length)
    [IO.File]::WriteAllText($Path,$updated,[Text.UTF8Encoding]::new($false))
}

$path = 'source/clients/gui/src/gui_main.cpp'

Replace-Exactly $path @'
    hStatus_ = CreateWindowExW(0, L"STATIC", L"● Initializing...",
        WS_VISIBLE | WS_CHILD | SS_SUNKEN,
        layout_.margin * 2 + layout_.button_w, layout_.margin, 300, layout_.button_h,
        hWinPanel, (HMENU)(INT_PTR)ID_STATUS, hInst_, nullptr);
'@ @'
    int statusX = layout_.margin * 3 + layout_.button_w * 2;
    hStatus_ = CreateWindowExW(0, L"STATIC", L"● Initializing...",
        WS_VISIBLE | WS_CHILD | SS_SUNKEN,
        statusX, layout_.margin, 300, layout_.button_h,
        hWinPanel, (HMENU)(INT_PTR)ID_STATUS, hInst_, nullptr);
'@

Replace-Exactly $path @'
    MoveWindow(hRefreshBtn_, layout_.margin, layout_.margin, layout_.button_w, layout_.button_h, TRUE);
    if (cw_content > layout_.margin * 2 + layout_.button_w + 100) {
      int br = layout_.margin * 2 + layout_.button_w;
      MoveWindow(hStatus_, br, layout_.margin, w - br - layout_.margin, layout_.button_h, TRUE);
    }
'@ @'
    MoveWindow(hRefreshBtn_, layout_.margin, layout_.margin, layout_.button_w, layout_.button_h, TRUE);
    int highlightX = layout_.margin * 2 + layout_.button_w;
    MoveWindow(hHighlightBtn_, highlightX, layout_.margin, layout_.button_w, layout_.button_h, TRUE);
    int statusX = layout_.margin * 3 + layout_.button_w * 2;
    if (cw_content > statusX + 100) {
      MoveWindow(hStatus_, statusX, layout_.margin, w - statusX - layout_.margin, layout_.button_h, TRUE);
    }
'@

Write-Host 'Hosted-native HCI toolbar layout normalized: Refresh, Highlight, and status no longer overlap.'
