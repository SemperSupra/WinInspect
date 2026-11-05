$ErrorActionPreference = 'Stop'

function Replace-Exactly {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Old,
        [Parameter(Mandatory=$true)][string]$New
    )
    $text = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $oldNorm = $Old.Replace("`r`n", "`n")
    $newNorm = $New.Replace("`r`n", "`n")
    $first = $text.IndexOf($oldNorm, [StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Expected source block not found in $Path" }
    if ($text.IndexOf($oldNorm, $first + $oldNorm.Length, [StringComparison]::Ordinal) -ge 0) {
        throw "Expected source block occurs more than once in $Path"
    }
    $updated = $text.Substring(0, $first) + $newNorm + $text.Substring($first + $oldNorm.Length)
    [IO.File]::WriteAllText($Path, $updated, [Text.UTF8Encoding]::new($false))
}

$path = 'source/clients/gui/src/gui_main.cpp'

# Owner-draw must preserve the labels of ordinary command buttons, not only sidebar tabs.
Replace-Exactly $path @'
  void onDrawItem(LPDRAWITEMSTRUCT dis) {
    if (dis->CtlType != ODT_BUTTON) return;
    auto& c = theme_.colors();
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    int tabId = (int)dis->CtlID;

    // Background: active tab gets highlight color, others get surface
    COLORREF bg = (tabId == active_tab_) ? c.highlight : c.surface;
    if (pressed) bg = c.accentHover;

    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, brush);
    DeleteObject(brush);

    // Active tab: blue left border
    if (tabId == active_tab_) {
      RECT br = dis->rcItem;
      br.right = br.left + 3;
      HBRUSH ab = CreateSolidBrush(c.accent);
      FillRect(dis->hDC, &br, ab);
      DeleteObject(ab);
    }

    // Text
    int idx = tabId - TAB_FIRST;
    if (idx >= 0 && idx < (TAB_COUNT - TAB_FIRST)) {
      std::wstring text = g_tabs[idx].label;
      SetBkMode(dis->hDC, TRANSPARENT);
      SetTextColor(dis->hDC, (tabId == active_tab_) ? c.accent : c.text);
      RECT tr = dis->rcItem;
      tr.left += 8;
      DrawTextW(dis->hDC, text.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
  }
'@ @'
  void onDrawItem(LPDRAWITEMSTRUCT dis) {
    if (dis->CtlType != ODT_BUTTON) return;
    auto& c = theme_.colors();
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    int controlId = (int)dis->CtlID;
    int idx = controlId - TAB_FIRST;
    bool isTab = idx >= 0 && idx < (TAB_COUNT - TAB_FIRST);
    bool activeTab = isTab && controlId == active_tab_;

    COLORREF bg = activeTab ? c.highlight : c.surface;
    if (pressed) bg = c.accentHover;

    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, brush);
    DeleteObject(brush);

    if (activeTab) {
      RECT br = dis->rcItem;
      br.right = br.left + 3;
      HBRUSH ab = CreateSolidBrush(c.accent);
      FillRect(dis->hDC, &br, ab);
      DeleteObject(ab);
    }

    wchar_t label[256] = {};
    GetWindowTextW(dis->hwndItem, label, (int)(sizeof(label) / sizeof(label[0])));
    std::wstring text = isTab ? g_tabs[idx].label : std::wstring(label);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, activeTab ? c.accent : c.text);
    RECT tr = dis->rcItem;
    UINT format = DT_VCENTER | DT_SINGLELINE;
    if (isTab) {
      tr.left += 8;
      format |= DT_LEFT;
    } else {
      format |= DT_CENTER;
    }
    DrawTextW(dis->hDC, text.c_str(), -1, &tr, format);

    if (dis->itemState & ODS_FOCUS) {
      RECT focus = dis->rcItem;
      InflateRect(&focus, -3, -3);
      DrawFocusRect(dis->hDC, &focus);
    }
  }
'@

# A disconnected desktop is an actionable state, not an exception path. Preserve stale
# inspection content, expose daemon recovery, and avoid replacing the state with raw RPC errors.
Replace-Exactly $path @'
  void refreshImpl(bool) {
    if (!transport_->connected()) setStatus(L"Disconnected — retrying...");
    try {
      vm_->refresh();
      TreeView_DeleteAllItems(hTree_);
      hwnd_storage_.clear();
      for (const auto& node : vm_->tree()) addNode(TVI_ROOT, node);
      // Refresh process list
      refreshProcessList();
      setStatus(transport_->connected() ? L"Connected" : L"Disconnected — showing last known state");
    }
    catch (const std::exception& e) {
      std::wstring werr(e.what(), e.what() + strlen(e.what()));
      setStatus((L"Error: " + werr).c_str());
    }
    catch (...) { setStatus(L"Unknown error"); }
    pollDaemonStatus();
  }
'@ @'
  void refreshImpl(bool) {
    if (!transport_->connected()) {
      setStatus(L"Daemon unavailable — start daemon to inspect this desktop");
      pollDaemonStatus();
      return;
    }
    try {
      vm_->refresh();
      TreeView_DeleteAllItems(hTree_);
      hwnd_storage_.clear();
      for (const auto& node : vm_->tree()) addNode(TVI_ROOT, node);
      // Refresh process list
      refreshProcessList();
      setStatus(L"Connected");
    }
    catch (const std::exception& e) {
      std::wstring werr(e.what(), e.what() + strlen(e.what()));
      setStatus((L"Refresh failed: " + werr).c_str());
    }
    catch (...) { setStatus(L"Refresh failed"); }
    pollDaemonStatus();
  }
'@

Write-Host 'Hosted-native HCI affordance repair applied.'
