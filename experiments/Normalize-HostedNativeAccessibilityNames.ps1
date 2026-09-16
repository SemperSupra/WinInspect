$ErrorActionPreference = 'Stop'
$path = 'source/clients/gui/src/gui_main.cpp'
$text = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")

function Replace-Exactly {
    param([string]$Old,[string]$New,[string]$Label)
    $first = $script:text.IndexOf($Old,[StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Expected $Label block not found." }
    if ($script:text.IndexOf($Old,$first + $Old.Length,[StringComparison]::Ordinal) -ge 0) {
        throw "Expected exactly one $Label block."
    }
    $script:text = $script:text.Substring(0,$first) + $New + $script:text.Substring($first + $Old.Length)
}

# Microsoft Win32 accessibility guidance: a STATIC label immediately preceding an
# Edit/ListView in z/tab order supplies the control's MSAA/UIA Name. The label may
# be non-visible and still be used by the Win32 accessibility proxy.
Replace-Exactly @'
    hSearchEdit_ = CreateWindowExW(0, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        layout_.margin + 75, layout_.margin, 120, tbH - 4, hwnd_, (HMENU)(INT_PTR)ID_SEARCH_EDIT, hInst_, nullptr);
'@ @'
    CreateWindowExW(0, L"STATIC", L"Search windows",
        WS_CHILD, 0, 0, 1, 1, hwnd_, nullptr, hInst_, nullptr);
    hSearchEdit_ = CreateWindowExW(0, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        layout_.margin + 75, layout_.margin, 120, tbH - 4, hwnd_, (HMENU)(INT_PTR)ID_SEARCH_EDIT, hInst_, nullptr);
    SendMessageW(hSearchEdit_, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search windows");
'@ 'search edit accessible label'

Replace-Exactly @'
    hList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT,
        layout_.split_w, layout_.toolbar_h, 100, 100,
        hWinPanel, (HMENU)(INT_PTR)ID_LIST, hInst_, nullptr);
'@ @'
    CreateWindowExW(0, L"STATIC", L"Window properties",
        WS_CHILD, 0, 0, 1, 1, hWinPanel, nullptr, hInst_, nullptr);
    hList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT,
        layout_.split_w, layout_.toolbar_h, 100, 100,
        hWinPanel, (HMENU)(INT_PTR)ID_LIST, hInst_, nullptr);
'@ 'window properties list accessible label'

[IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
Write-Host 'Hosted-native accessible names normalized for Search and Window properties controls.'
