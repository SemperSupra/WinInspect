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
  bool translate_accel(MSG* msg) { return hAccel_ ? TranslateAcceleratorW(msg->hwnd, hAccel_, msg) != 0 : false; }

  void show(int nCmdShow) {
'@ @'
  bool translate_accel(MSG* msg) { return hAccel_ ? TranslateAcceleratorW(msg->hwnd, hAccel_, msg) != 0 : false; }
  bool translate_dialog(MSG* msg) { return hwnd_ ? IsDialogMessageW(hwnd_, msg) != 0 : false; }

  void show(int nCmdShow) {
'@ 'dialog-message hook'

Replace-Exactly @'
    // Show Windows tab, hide others
    switchTab(active_tab_);
    applyTheme();
  }
'@ @'
    // Enable native dialog-style keyboard traversal through nested tab panels.
    // WS_EX_CONTROLPARENT lets IsDialogMessage recurse into each active panel;
    // WS_TABSTOP marks standard interactive controls as keyboard destinations.
    for (auto& panel : hPanels_) {
      if (!panel) continue;
      LONG_PTR ex = GetWindowLongPtrW(panel, GWL_EXSTYLE);
      SetWindowLongPtrW(panel, GWL_EXSTYLE, ex | WS_EX_CONTROLPARENT);
    }
    EnumChildWindows(hwnd_, [](HWND child, LPARAM) -> BOOL {
      wchar_t cls[64] = {};
      if (!GetClassNameW(child, cls, static_cast<int>(sizeof(cls) / sizeof(cls[0]))))
        return TRUE;
      const bool tab_target =
          _wcsicmp(cls, L"Button") == 0 ||
          _wcsicmp(cls, L"Edit") == 0 ||
          _wcsicmp(cls, L"ComboBox") == 0 ||
          _wcsicmp(cls, WC_TREEVIEWW) == 0 ||
          _wcsicmp(cls, WC_LISTVIEWW) == 0;
      if (tab_target) {
        LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
        if ((style & WS_TABSTOP) == 0)
          SetWindowLongPtrW(child, GWL_STYLE, style | WS_TABSTOP);
      }
      return TRUE;
    }, 0);

    // Show Windows tab, hide others
    switchTab(active_tab_);
    applyTheme();
  }
'@ 'tab-stop normalization'

Replace-Exactly @'
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (!win.translate_accel(&msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
'@ @'
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (!win.translate_accel(&msg) && !win.translate_dialog(&msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
'@ 'message-loop keyboard traversal'

[IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
Write-Host 'Hosted-native keyboard access normalized: WS_TABSTOP targets, WS_EX_CONTROLPARENT panels, and IsDialogMessage traversal installed.'
