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
    active_tab_ = TAB_WINDOWS;
    createControls();
    initializeAccessibleNames();
    refresh();
'@ @'
    active_tab_ = TAB_WINDOWS;

    // Construct the child hierarchy once in canonical 96-DPI logical geometry.
    // This keeps literal panel layouts meaningful and gives every child a stable
    // logical rectangle that can be replayed at any monitor DPI without compounding.
    const int startupDpi = dpi_;
    layout_ = Layout{};
    createControls();
    initializeAccessibleNames();
    captureLogicalChildLayout();
    layout_ = Layout{};
    layout_.scale(startupDpi);
    applyLogicalChildLayout();
    onSize();
    refresh();
'@ 'canonical child-layout initialization'

Replace-Exactly @'
  std::vector<std::string> hwnd_storage_;

  // ── Window procedure ──────────────────────────────────────────────────
'@ @'
  std::vector<std::string> hwnd_storage_;

  struct LogicalChildRect {
    HWND hwnd;
    int x;
    int y;
    int width;
    int height;
  };
  struct LogicalListColumn {
    HWND list;
    int column;
    int width;
  };
  std::vector<LogicalChildRect> logical_child_rects_;
  std::vector<LogicalListColumn> logical_list_columns_;

  // ── Window procedure ──────────────────────────────────────────────────
'@ 'logical child-layout state'

Replace-Exactly @'
  // ── Accessibility ─────────────────────────────────────────────────────
'@ @'
  // ── Per-monitor child layout ──────────────────────────────────────────

  void captureLogicalChildLayout() {
    logical_child_rects_.clear();
    logical_list_columns_.clear();
    EnumChildWindows(hwnd_, [](HWND child, LPARAM param) -> BOOL {
      auto* self = reinterpret_cast<WinInspectWindow*>(param);
      HWND parent = GetParent(child);
      if (!parent) return TRUE;

      RECT r = {};
      if (GetWindowRect(child, &r)) {
        POINT points[2] = {{r.left, r.top}, {r.right, r.bottom}};
        MapWindowPoints(HWND_DESKTOP, parent, points, 2);
        self->logical_child_rects_.push_back({
            child,
            points[0].x,
            points[0].y,
            points[1].x - points[0].x,
            points[1].y - points[0].y});
      }

      wchar_t cls[64] = {};
      if (GetClassNameW(child, cls, static_cast<int>(sizeof(cls) / sizeof(cls[0]))) &&
          _wcsicmp(cls, WC_LISTVIEWW) == 0) {
        HWND header = ListView_GetHeader(child);
        int count = header ? Header_GetItemCount(header) : 0;
        for (int column = 0; column < count; ++column) {
          self->logical_list_columns_.push_back(
              {child, column, ListView_GetColumnWidth(child, column)});
        }
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(this));
  }

  void applyLogicalChildLayout() {
    for (const auto& item : logical_child_rects_) {
      if (!IsWindow(item.hwnd)) continue;
      MoveWindow(item.hwnd,
                 MulDiv(item.x, dpi_, 96),
                 MulDiv(item.y, dpi_, 96),
                 MulDiv(item.width, dpi_, 96),
                 MulDiv(item.height, dpi_, 96), TRUE);
    }
    for (const auto& column : logical_list_columns_) {
      if (!IsWindow(column.list)) continue;
      ListView_SetColumnWidth(column.list, column.column,
                              MulDiv(column.width, dpi_, 96));
    }
  }

  // ── Accessibility ─────────────────────────────────────────────────────
'@ 'generic per-monitor child-layout helpers'

Replace-Exactly @'
  void onDpiChanged(WPARAM wParam, LPARAM lParam) {
    dpi_ = HIWORD(wParam); layout_.scale(dpi_);
    RECT* r = (RECT*)lParam;
    SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    applyTheme(); onSize();
  }
'@ @'
  void onDpiChanged(WPARAM wParam, LPARAM lParam) {
    dpi_ = HIWORD(wParam);
    layout_ = Layout{};
    layout_.scale(dpi_);
    RECT* r = (RECT*)lParam;
    SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    applyLogicalChildLayout();
    applyTheme();
    onSize();
  }
'@ 'all-panel DPI replay'

[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
Write-Host 'All-panel DPI candidate installed: canonical 96-DPI child rectangles and ListView columns replay at current monitor DPI.'
