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

Replace-Exactly @'
struct Layout {
  int margin = 6;
  int button_w = 80;
  int button_h = 24;
  int toolbar_h = 36;
  int sidebar_w = SIDEBAR_W;
  int sidebar_btn_h = SIDEBAR_BTN_H;
  int split_w = 250;
  int min_w = 800;
  int min_h = 500;
  int col_prop = 150;
  int col_val = 300;

  void scale(int dpi) {
    double f = dpi / 96.0;
    margin = int(margin * f);
    button_w = int(button_w * f);
    button_h = int(button_h * f);
    toolbar_h = int(toolbar_h * f);
    sidebar_w = int(sidebar_w * f);
    sidebar_btn_h = int(sidebar_btn_h * f);
    split_w = int(split_w * f);
    min_w = int(min_w * f);
    min_h = int(min_h * f);
    col_prop = int(col_prop * f);
    col_val = int(col_val * f);
  }
};
'@ @'
struct Layout {
  int margin = 6;
  int button_w = 80;
  int button_h = 24;
  int toolbar_h = 36;
  int sidebar_w = SIDEBAR_W;
  int sidebar_btn_h = SIDEBAR_BTN_H;
  int split_w = 250;
  int min_w = 800;
  int min_h = 500;
  int col_prop = 150;
  int col_val = 300;

  // Global toolbar logical dimensions. Keep them in the same 96-DPI coordinate
  // system as the rest of Layout so per-monitor transitions are reversible.
  int global_toolbar_h = 30;
  int connect_w = 70;
  int connect_h = 26;
  int search_offset = 75;
  int search_w = 120;
  int search_h = 24;
  int daemon_offset = 205;
  int daemon_w = 160;
  int start_daemon_w = 110;

  void scale(int dpi) {
    // Always derive physical pixels from immutable 96-DPI logical constants.
    // Multiplying the previous values compounds scale across monitor changes.
    margin = MulDiv(6, dpi, 96);
    button_w = MulDiv(80, dpi, 96);
    button_h = MulDiv(24, dpi, 96);
    toolbar_h = MulDiv(36, dpi, 96);
    sidebar_w = MulDiv(SIDEBAR_W, dpi, 96);
    sidebar_btn_h = MulDiv(SIDEBAR_BTN_H, dpi, 96);
    split_w = MulDiv(250, dpi, 96);
    min_w = MulDiv(800, dpi, 96);
    min_h = MulDiv(500, dpi, 96);
    col_prop = MulDiv(150, dpi, 96);
    col_val = MulDiv(300, dpi, 96);
    global_toolbar_h = MulDiv(30, dpi, 96);
    connect_w = MulDiv(70, dpi, 96);
    connect_h = MulDiv(26, dpi, 96);
    search_offset = MulDiv(75, dpi, 96);
    search_w = MulDiv(120, dpi, 96);
    search_h = MulDiv(24, dpi, 96);
    daemon_offset = MulDiv(205, dpi, 96);
    daemon_w = MulDiv(160, dpi, 96);
    start_daemon_w = MulDiv(110, dpi, 96);
  }
};
'@ 'reversible Layout scaling'

Replace-Exactly @'
    // Toolbar: connect button + search
    int tbH = 30;
    hConnectBtn_ = CreateWindowExW(0, L"BUTTON", L"Connect",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        layout_.margin, layout_.margin, 70, tbH - 4, hwnd_, (HMENU)(INT_PTR)ID_CONNECT_BTN, hInst_, nullptr);
    hSearchEdit_ = CreateWindowExW(0, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        layout_.margin + 75, layout_.margin, 120, tbH - 4, hwnd_, (HMENU)(INT_PTR)ID_SEARCH_EDIT, hInst_, nullptr);

    // Daemon status indicator (colored dot with text)
    int indicatorX = layout_.margin + 205;
    hDaemonIndicator_ = CreateWindowExW(0, L"STATIC", L"● Daemon: checking...",
        WS_VISIBLE | WS_CHILD | SS_SUNKEN,
        indicatorX, layout_.margin, 160, tbH - 4, hwnd_, nullptr, hInst_, nullptr);

    // Start Daemon button (hidden by default, shown when daemon is not running)
    hStartDaemonBtn_ = CreateWindowExW(0, L"BUTTON", L"Start Daemon",
        WS_CHILD | BS_OWNERDRAW,
        indicatorX, layout_.margin, 110, tbH - 4, hwnd_, (HMENU)(INT_PTR)ID_START_DAEMON_BTN, hInst_, nullptr);
'@ @'
    // Global toolbar: all dimensions are physical values derived from 96-DPI logical units.
    int tbH = layout_.global_toolbar_h;
    hConnectBtn_ = CreateWindowExW(0, L"BUTTON", L"Connect",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        layout_.margin, (tbH - layout_.connect_h) / 2, layout_.connect_w, layout_.connect_h,
        hwnd_, (HMENU)(INT_PTR)ID_CONNECT_BTN, hInst_, nullptr);
    hSearchEdit_ = CreateWindowExW(0, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        layout_.margin + layout_.search_offset, (tbH - layout_.search_h) / 2,
        layout_.search_w, layout_.search_h, hwnd_, (HMENU)(INT_PTR)ID_SEARCH_EDIT, hInst_, nullptr);

    // Daemon status indicator (colored dot with text)
    int indicatorX = layout_.margin + layout_.daemon_offset;
    hDaemonIndicator_ = CreateWindowExW(0, L"STATIC", L"● Daemon: checking...",
        WS_VISIBLE | WS_CHILD | SS_SUNKEN,
        indicatorX, (tbH - layout_.connect_h) / 2, layout_.daemon_w, layout_.connect_h,
        hwnd_, nullptr, hInst_, nullptr);

    // Start Daemon button (hidden by default, shown when daemon is not running)
    hStartDaemonBtn_ = CreateWindowExW(0, L"BUTTON", L"Start Daemon",
        WS_CHILD | BS_OWNERDRAW,
        indicatorX, (tbH - layout_.connect_h) / 2, layout_.start_daemon_w, layout_.connect_h,
        hwnd_, (HMENU)(INT_PTR)ID_START_DAEMON_BTN, hInst_, nullptr);
'@ 'DPI-aware toolbar creation'

Replace-Exactly @'
    // Toolbar row
    int tbH = 30;
    if (hConnectBtn_) MoveWindow(hConnectBtn_, layout_.margin, (tbH - 26) / 2, 70, 26, TRUE);
    if (hSearchEdit_) MoveWindow(hSearchEdit_, layout_.margin + 75, (tbH - 24) / 2, 120, 24, TRUE);

    // Sidebar (below toolbar)
'@ @'
    // Global toolbar row
    int tbH = layout_.global_toolbar_h;
    if (hConnectBtn_) MoveWindow(hConnectBtn_, layout_.margin, (tbH - layout_.connect_h) / 2,
                                 layout_.connect_w, layout_.connect_h, TRUE);
    if (hSearchEdit_) MoveWindow(hSearchEdit_, layout_.margin + layout_.search_offset,
                                 (tbH - layout_.search_h) / 2,
                                 layout_.search_w, layout_.search_h, TRUE);
    int indicatorX = layout_.margin + layout_.daemon_offset;
    if (hDaemonIndicator_) MoveWindow(hDaemonIndicator_, indicatorX, (tbH - layout_.connect_h) / 2,
                                      layout_.daemon_w, layout_.connect_h, TRUE);
    if (hStartDaemonBtn_) MoveWindow(hStartDaemonBtn_, indicatorX, (tbH - layout_.connect_h) / 2,
                                     layout_.start_daemon_w, layout_.connect_h, TRUE);

    // Sidebar (below toolbar)
'@ 'DPI-aware toolbar relayout'

[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
Write-Host 'DPI candidate installed: reversible 96-DPI-derived layout scaling and global toolbar scaling.'
