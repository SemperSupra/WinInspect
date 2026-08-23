// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// WinInspect Native GUI — Win32/Wine compatible.
// Sidebar navigation with theme engine (Light/Dark/System).

#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

// Enable Common Controls v6 via manifest dependency
#pragma comment(linker, \
  "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
  "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <commctrl.h>
#include <memory>
#include <string>
#include <vector>
#include <future>
#include <chrono>
#include <algorithm>
#include <cstdio>

#include "viewmodel.hpp"
#include "wininspect/tinyjson.hpp"
#include "wininspect/types.hpp"
#include "theme.hpp"
#include "transport.hpp"

#pragma comment(lib, "comctl32.lib")

using namespace wininspect_gui;

// ── Tab definitions ─────────────────────────────────────────────────────

struct TabInfo {
  int id;
  const wchar_t* icon;
  const wchar_t* label;
};

static constexpr int TAB_FIRST = 1000;
enum TabId : int {
  TAB_DASHBOARD = TAB_FIRST,
  TAB_WINDOWS,
  TAB_CAPTURE,
  TAB_INPUT,
  TAB_SESSIONS,
  TAB_EVENTS,
  TAB_METRICS,
  TAB_PROCESSES,
  TAB_COUNT,
};

static const TabInfo g_tabs[TAB_COUNT - TAB_FIRST] = {
  {TAB_DASHBOARD, L"", L"Dashboard"},
  {TAB_WINDOWS,   L"", L"Windows"},
  {TAB_CAPTURE,   L"", L"Capture"},
  {TAB_INPUT,     L"", L"Input"},
  {TAB_SESSIONS,  L"", L"Sessions"},
  {TAB_EVENTS,    L"", L"Events"},
  {TAB_METRICS,   L"", L"Metrics"},
  {TAB_PROCESSES, L"", L"Processes"},
};

static constexpr int SIDEBAR_W = 200;
static constexpr int SIDEBAR_BTN_H = 32;

// ── Control IDs ─────────────────────────────────────────────────────────

enum CtrlId : int {
  ID_REFRESH_BTN = 201,
  ID_HIGHLIGHT_BTN = 203,
  ID_KILL_BTN = 204,
  ID_START_DAEMON_BTN = 205,
  ID_STATUS = 202,
  ID_TREE = 101,
  ID_LIST = 102,
  ID_THEME_LIGHT = 901,
  ID_THEME_DARK = 902,
  ID_THEME_SYSTEM = 903,
  ID_CONNECT_BTN = 601,
  ID_SEARCH_EDIT = 602,
  ID_ABOUT_CMD = 701,
  ID_DLG_HOST = 801,
  ID_DLG_PORT = 802,
  ID_DLG_AUTH = 803,
  ID_DLG_CONNECT = 804,
};

// ── Layout ──────────────────────────────────────────────────────────────

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

// ── Main Window ─────────────────────────────────────────────────────────

class WinInspectWindow {
public:
  bool init(HINSTANCE hInst) {
    hInst_ = hInst; layout_ = Layout{};

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst_;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"WinInspectGUI";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(hInst_, MAKEINTRESOURCE(101));
    if (!RegisterClassExW(&wc)) return false;

    dpi_ = GetDpiForSystem(); layout_.scale(dpi_);

    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"WinInspect",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            layout_.min_w, layout_.min_h,
                            nullptr, nullptr, hInst_, this);
    if (!hwnd_) return false;

    // Theme and About menus
    hMenu_ = CreateMenu();
    HMENU viewMenu = CreatePopupMenu();
    AppendMenuW(viewMenu, MF_STRING, ID_THEME_LIGHT, L"Light");
    AppendMenuW(viewMenu, MF_STRING, ID_THEME_DARK, L"Dark");
    AppendMenuW(viewMenu, MF_STRING, ID_THEME_SYSTEM, L"System");
    AppendMenuW(hMenu_, MF_POPUP, (UINT_PTR)viewMenu, L"View");
    HMENU helpMenu = CreatePopupMenu();
    AppendMenuW(helpMenu, MF_STRING, ID_ABOUT_CMD, L"About WinInspect");
    AppendMenuW(hMenu_, MF_POPUP, (UINT_PTR)helpMenu, L"Help");
    SetMenu(hwnd_, hMenu_);

    transport_ = std::make_unique<PipeTransport>();
    vm_ = std::make_unique<ViewModel>(transport_.get());
    active_tab_ = TAB_WINDOWS;
    createControls();
    refresh();
    SetTimer(hwnd_, IDT_REFRESH, 5000, nullptr);
    return true;
  }

  bool translate_accel(MSG* msg) { return hAccel_ ? TranslateAcceleratorW(msg->hwnd, hAccel_, msg) != 0 : false; }

  void show(int nCmdShow) {
    // Create accelerator table for keyboard shortcuts
    ACCEL accels[] = {
      {FVIRTKEY, VK_F5,     ID_REFRESH_BTN},       // F5 = Refresh
      {FVIRTKEY | FCONTROL, 'H', ID_HIGHLIGHT_BTN}, // Ctrl+H = Highlight
      {FVIRTKEY, VK_F1,     ID_ABOUT_CMD},          // F1 = About
    };
    hAccel_ = CreateAcceleratorTableW(accels, sizeof(accels) / sizeof(ACCEL));
    ShowWindow(hwnd_, nCmdShow); UpdateWindow(hwnd_);
  }

private:
  HACCEL hAccel_ = nullptr;
  static constexpr int IDT_REFRESH = 1;
  static constexpr int IDT_RETRY = 2;
  static constexpr int WM_APP_SET_STATUS = WM_APP + 2;

  Layout layout_;
  int dpi_ = 96;
  ThemeManager theme_;
  HMENU hMenu_ = nullptr;
  HFONT hFont_ = nullptr;
  int active_tab_ = TAB_WINDOWS;
  HWND hwnd_ = nullptr;

  // Sidebar + panels
  HWND hSidebar_ = nullptr;
  HWND hPanels_[TAB_COUNT - TAB_FIRST] = {};

  // Windows tab controls
  HWND hTree_ = nullptr;
  HWND hList_ = nullptr;
  HWND hRefreshBtn_ = nullptr;
  HWND hHighlightBtn_ = nullptr;
  HWND hStatus_ = nullptr;

  // Dashboard tab controls
  HWND hStatCards_[4] = {};
  HWND hEventsList_ = nullptr;

  // Capture tab controls
  HWND hCapturePreview_ = nullptr;
  HWND hCaptureBtn_ = nullptr;
  HWND hCaptureRegionBtn_ = nullptr;
  HWND hCaptureWindowBtn_ = nullptr;
  HWND hCaptureStrategy_ = nullptr;
  HWND hCaptureFormat_ = nullptr;

  // Input tab controls
  HWND hMouseBtns_[9] = {};
  HWND hLeftClickBtn_ = nullptr;
  HWND hRightClickBtn_ = nullptr;
  HWND hInputText_ = nullptr;
  HWND hTextSendBtn_ = nullptr;
  HWND hInputHotkey_ = nullptr;
  HWND hHotkeySendBtn_ = nullptr;
  HWND hInputStatus_ = nullptr;

  // Toolbar controls
  HWND hConnectBtn_ = nullptr;
  HWND hSearchEdit_ = nullptr;
  HWND hDaemonIndicator_ = nullptr;
  HWND hStartDaemonBtn_ = nullptr;

  // Sessions/Events/Metrics/Processes controls
  HWND hSessList_ = nullptr;
  HWND hSessStatus_ = nullptr;
  HWND hEventLogList_ = nullptr;
  HWND hMetricCards_[4] = {};
  HWND hMetricsList_ = nullptr;
  HWND hProcList_ = nullptr;
  HWND hProcKillBtn_ = nullptr;
  HINSTANCE hInst_ = nullptr;
  DWORD main_thread_id_ = GetCurrentThreadId();
  std::atomic_flag refresh_running_ = ATOMIC_FLAG_INIT;
  COLORREF daemon_indicator_color_ = RGB(0x2e, 0x7d, 0x32); // green default
  std::unique_ptr<ViewModel> vm_;
  std::unique_ptr<PipeTransport> transport_;
  std::vector<std::string> hwnd_storage_;

  // ── Window procedure ──────────────────────────────────────────────────

  static LRESULT CALLBACK wndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    WinInspectWindow* self = nullptr;
    if (uMsg == WM_NCCREATE) {
      self = (WinInspectWindow*)((CREATESTRUCT*)lParam)->lpCreateParams;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else
      self = (WinInspectWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!self) return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    switch (uMsg) {
      case WM_SIZE:        self->onSize(); return 0;
      case WM_GETMINMAXINFO: self->onMinMaxInfo((MINMAXINFO*)lParam); return 0;
      case WM_CTLCOLORBTN: case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX:
      case WM_CTLCOLOREDIT: case WM_CTLCOLORSCROLLBAR:
        return self->onCtlColor(uMsg, wParam, lParam);
      case WM_COMMAND:     self->onCommand(LOWORD(wParam)); return 0;
      case WM_TIMER:       self->onTimer(wParam); return 0;
      case WM_NOTIFY:      self->onNotify(lParam); return 0;
      case WM_DPICHANGED:  self->onDpiChanged(wParam, lParam); return 0;
      case WM_SETTINGCHANGE:
        if (lParam) { std::wstring a((LPCWSTR)lParam); if (a == L"ImmersiveColorSet") self->onThemeChanged(); }
        return 0;
      case WM_DRAWITEM:    self->onDrawItem((LPDRAWITEMSTRUCT)lParam); return TRUE;
      case WM_APP_SET_STATUS:
        self->setStatus((const wchar_t*)lParam);
        if (lParam) free((void*)lParam);
        return 0;
      case WM_DESTROY:
        KillTimer(hwnd, IDT_REFRESH); KillTimer(hwnd, IDT_RETRY);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  }

  LRESULT onCtlColor(UINT, WPARAM wParam, LPARAM lParam) {
    HDC hdc = (HDC)wParam; HWND hc = (HWND)lParam;
    SetBkMode(hdc, TRANSPARENT);
    auto& c = theme_.colors();

    // Stat cards: surface background, dim text for label, bright text for value
    for (int i = 0; i < 4; i++) {
      if (hc == hStatCards_[i]) {
        SetBkColor(hdc, c.surface);
        return (LRESULT)theme_.surface_brush();
      }
    }

    // Capture preview: darker background
    if (hc == hCapturePreview_) {
      SetBkColor(hdc, RGB(0x11, 0x13, 0x18));
      SetTextColor(hdc, RGB(0x55, 0x55, 0x55));
      return (LRESULT)GetStockObject(BLACK_BRUSH);
    }

    if (hc == hDaemonIndicator_) {
      SetTextColor(hdc, daemon_indicator_color_);
      SetBkColor(hdc, c.background);
      return (LRESULT)theme_.background_brush();
    }
    if (hc == hStatus_) {
      SetTextColor(hdc, transport_->connected() ? RGB(0x2e, 0x7d, 0x32) : RGB(0xc6, 0x28, 0x28));
      SetBkColor(hdc, c.background);
      return (LRESULT)theme_.background_brush();
    }
    if (hc == hSidebar_) {
      SetTextColor(hdc, c.text);
      SetBkColor(hdc, c.background);
      return (LRESULT)theme_.background_brush();
    }
    SetTextColor(hdc, c.text);
    return (LRESULT)theme_.surface_brush();
  }

  void checkThemeMenuItem() {
    auto m = theme_.mode();
    CheckMenuItem(hMenu_, ID_THEME_LIGHT, MF_BYCOMMAND | (m == ThemeMode::Light ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu_, ID_THEME_DARK, MF_BYCOMMAND | (m == ThemeMode::Dark ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu_, ID_THEME_SYSTEM, MF_BYCOMMAND | (m == ThemeMode::System ? MF_CHECKED : MF_UNCHECKED));
  }

  void onThemeChanged() { theme_.on_system_theme_changed(); applyTheme(); }

  /// Create a UI font — prefer Segoe UI on Windows, fall back to MS Shell Dlg 2 on Wine.
  static HFONT create_ui_font(int dpi)
  {
    // Try Segoe UI first (Windows 10/11 native)
    HFONT f = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    // Verify the font was created and has the right face
    if (f) {
      wchar_t face[LF_FACESIZE];
      HDC dc = GetDC(nullptr);
      HFONT old = (HFONT)SelectObject(dc, f);
      GetTextFaceW(dc, LF_FACESIZE, face);
      SelectObject(dc, old);
      ReleaseDC(nullptr, dc);
      if (wcsicmp(face, L"Segoe UI") == 0)
        return f; // Segoe UI confirmed — Windows 10/11
      DeleteObject(f);
    }
    // Fallback: MS Shell Dlg 2 (maps to Tahoma on Win7, MS Sans Serif on older, clean on Wine)
    return CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DEFAULT_QUALITY, DEFAULT_PITCH, L"MS Shell Dlg 2");
  }

  void applyTheme() {
    theme_.create_brushes();
    if (hFont_) DeleteObject(hFont_);
    hFont_ = create_ui_font(dpi_);
    if (hFont_) {
      for (auto& h : hPanels_) if (h) SendMessageW(h, WM_SETFONT, (WPARAM)hFont_, TRUE);
      SendMessageW(hTree_, WM_SETFONT, (WPARAM)hFont_, TRUE);
      SendMessageW(hList_, WM_SETFONT, (WPARAM)hFont_, TRUE);
      SendMessageW(hRefreshBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);
      SendMessageW(hStatus_, WM_SETFONT, (WPARAM)hFont_, TRUE);
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
    checkThemeMenuItem();
  }

  // ── Custom draw for list/tree ─────────────────────────────────────────

  void onCustomDraw(LPARAM lParam) {
    auto lvcd = (LPNMLVCUSTOMDRAW)lParam;
    if (lvcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
      SetWindowLongPtrW(hwnd_, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW); return;
    }
    if (lvcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
      lvcd->clrText = theme_.colors().text;
      lvcd->clrTextBk = theme_.colors().surface;
      SetWindowLongPtrW(hwnd_, DWLP_MSGRESULT, CDRF_DODEFAULT);
    }
  }

  void onTreeCustomDraw(LPARAM lParam) {
    auto tvcd = (LPNMTVCUSTOMDRAW)lParam;
    if (tvcd->nmcd.dwDrawStage == CDDS_PREPAINT) {
      SetWindowLongPtrW(hwnd_, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW); return;
    }
    if (tvcd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
      tvcd->clrText = theme_.colors().text;
      tvcd->clrTextBk = theme_.colors().surface;
      SetWindowLongPtrW(hwnd_, DWLP_MSGRESULT, CDRF_DODEFAULT);
    }
  }

  // ── Owner-draw sidebar buttons ────────────────────────────────────────

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

  // ── Commands ──────────────────────────────────────────────────────────

  void onCommand(int id) {
    if (id >= TAB_FIRST && id < TAB_FIRST + (TAB_COUNT - TAB_FIRST)) {
      switchTab(id);
      return;
    }
    // Handle mouse direction buttons (401-409)
    if (id >= 401 && id <= 409) { onMouseDir(id); return; }
    switch (id) {
      case ID_REFRESH_BTN: refresh(); return;
      case ID_HIGHLIGHT_BTN: onHighlight(); return;
      case ID_KILL_BTN: onKillProcess(); return;
      case ID_START_DAEMON_BTN: onStartDaemon(); return;
      case ID_CONNECT_BTN: onConnect(); return;
      case ID_ABOUT_CMD: onAbout(); return;
      case 301: case 302: case 303: onCapture(id); return;
      case 410: onMouseClick(L"left"); return;
      case 411: onMouseClick(L"right"); return;
      case 420: onSendText(); return;
      case 430: onSendHotkey(); return;
      case ID_THEME_LIGHT: theme_.set_mode(ThemeMode::Light); applyTheme(); return;
      case ID_THEME_DARK:  theme_.set_mode(ThemeMode::Dark); applyTheme(); return;
      case ID_THEME_SYSTEM: theme_.set_mode(ThemeMode::System); applyTheme(); return;
    }
  }

  void switchTab(int tabId) {
    active_tab_ = tabId;
    for (int i = 0; i < (TAB_COUNT - TAB_FIRST); i++)
      if (hPanels_[i])
        ShowWindow(hPanels_[i], (TAB_FIRST + i) == tabId ? SW_SHOW : SW_HIDE);
    InvalidateRect(hSidebar_, nullptr, TRUE);
  }

  void onMouseDir(int id) {
    const wchar_t* dirs[] = {L"up-left",L"up",L"up-right",L"left",L"center",L"right",L"down-left",L"down",L"down-right"};
    int idx = id - 401;
    if (idx >= 0 && idx < 9) onInputAction(std::wstring(L"Mouse: ") + dirs[idx]);
  }

  void onMouseClick(const wchar_t* btn) {
    onInputAction(std::wstring(L"Mouse click: ") + btn);
  }

  void onSendText() {
    wchar_t buf[256] = {}; GetWindowTextW(hInputText_, buf, 256);
    onInputAction(std::wstring(L"Text: ") + buf);
  }

  void onSendHotkey() {
    wchar_t buf[256] = {}; GetWindowTextW(hInputHotkey_, buf, 256);
    onInputAction(std::wstring(L"Hotkey: ") + buf);
  }

  void onInputAction(const std::wstring& msg) {
    SetWindowTextW(hInputStatus_, msg.c_str());
    setStatus(msg);
  }

  void onConnect() {
    setStatus(L"Connect dialog — coming in future update");
  }

  void onAbout() {
    std::wstring theme = theme_.mode() == ThemeMode::Light ? L"Light" :
                         theme_.mode() == ThemeMode::Dark ? L"Dark" : L"System";
    std::string ver{wininspect::WININSPECT_VERSION.data(), wininspect::WININSPECT_VERSION.size()};
    std::wstring wver(ver.begin(), ver.end());
    std::wstring msg =
        L"WinInspect " + wver + L"\n"
        L"Window inspection for Windows and Wine\n"
        L"\n"
        L"• Theme: " + theme + L"\n"
        L"• Runtime: " +
#ifdef _WIN32
        L"Native Windows" +
#else
        L"Wine" +
#endif
        L"\n"
        L"• License: PolyForm Noncommercial 1.0.0\n"
        L"  Free for open-source and non-commercial use.\n"
        L"  Commercial licenses available on request.\n"
        L"  Contact: mark.e.deyoung+wininspect-license@gmail.com\n"
        L"\n"
        L"“Strix the Window Owl” inspects Windows\n"
        L"and Wine desktops on your behalf.\n"
        L"\n"
        L"Support the project:\n"
        L"  GitHub Sponsors: github.com/sponsors/mark-e-deyoung\n"
        L"  Ko-fi: ko-fi.com/peaceactivist\n"
        L"\n"
        L"Copyright © 2026 Mark E. DeYoung";
    MessageBoxW(hwnd_, msg.c_str(), L"About WinInspect", MB_OK | MB_ICONINFORMATION);
  }

  void onCapture(int id) {
    const wchar_t* mode = id == 301 ? L"Full Screen" : (id == 302 ? L"Region" : L"Window");
    int sel = (int)SendMessageW(hCaptureStrategy_, CB_GETCURSEL, 0, 0);
    wchar_t strategy[32] = {}; SendMessageW(hCaptureStrategy_, CB_GETLBTEXT, sel, (LPARAM)strategy);
    setStatus((std::wstring(L"Capture: ") + mode + L" via " + strategy).c_str());
  }

  void onTimer(WPARAM id) {
    if (id == IDT_REFRESH) {
      pollDaemonStatus();
      if (transport_->connected()) setStatus(L"Connected");
      refreshAsync();
    }
    else if (id == IDT_RETRY) { KillTimer(hwnd_, IDT_RETRY); refresh(); }
  }

  // ── Controls ──────────────────────────────────────────────────────────

  void createControls() {
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

    // Sidebar panel (below toolbar)
    hSidebar_ = CreateWindowExW(0, L"STATIC", L"",
        WS_VISIBLE | WS_CHILD | SS_NOTIFY,
        0, tbH, layout_.sidebar_w, 100,
        hwnd_, nullptr, hInst_, nullptr);

    // Sidebar buttons (owner-draw for each tab)
    for (int i = 0; i < (TAB_COUNT - TAB_FIRST); i++) {
      CreateWindowExW(0, L"BUTTON", g_tabs[i].label,
          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
          0, i * layout_.sidebar_btn_h, layout_.sidebar_w, layout_.sidebar_btn_h,
          hwnd_, (HMENU)(INT_PTR)(TAB_FIRST + i), hInst_, nullptr);
    }

    // ── Windows tab content ───────────────────────────────────────────
    HWND hWinPanel = CreateWindowExW(0, L"STATIC", L"",
        WS_VISIBLE | WS_CHILD,
        0, 0, 100, 100, hwnd_, nullptr, hInst_, nullptr);
    hPanels_[TAB_WINDOWS - TAB_FIRST] = hWinPanel;

    hRefreshBtn_ = CreateWindowExW(0, L"BUTTON", L"Refresh",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        layout_.margin, layout_.margin, layout_.button_w, layout_.button_h,
        hWinPanel, (HMENU)(INT_PTR)ID_REFRESH_BTN, hInst_, nullptr);

    hHighlightBtn_ = CreateWindowExW(0, L"BUTTON", L"Highlight",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        layout_.margin * 2 + layout_.button_w, layout_.margin, layout_.button_w, layout_.button_h,
        hWinPanel, (HMENU)(INT_PTR)ID_HIGHLIGHT_BTN, hInst_, nullptr);

    hStatus_ = CreateWindowExW(0, L"STATIC", L"● Initializing...",
        WS_VISIBLE | WS_CHILD | SS_SUNKEN,
        layout_.margin * 2 + layout_.button_w, layout_.margin, 300, layout_.button_h,
        hWinPanel, (HMENU)(INT_PTR)ID_STATUS, hInst_, nullptr);

    hTree_ = CreateWindowExW(0, WC_TREEVIEWW, L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_HASLINES,
        0, layout_.toolbar_h, layout_.split_w, 100,
        hWinPanel, (HMENU)(INT_PTR)ID_TREE, hInst_, nullptr);

    hList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT,
        layout_.split_w, layout_.toolbar_h, 100, 100,
        hWinPanel, (HMENU)(INT_PTR)ID_LIST, hInst_, nullptr);

    ListView_SetExtendedListViewStyle(hList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = layout_.col_prop; col.pszText = (LPWSTR)L"Property";
    ListView_InsertColumn(hList_, 0, &col);
    col.cx = layout_.col_val; col.pszText = (LPWSTR)L"Value";
    ListView_InsertColumn(hList_, 1, &col);

    // ── Dashboard tab ─────────────────────────────────────────────────
    {
      HWND hPanel = CreateWindowExW(0, L"STATIC", L"",
          WS_VISIBLE | WS_CHILD, 0, 0, 100, 100, hwnd_, nullptr, hInst_, nullptr);
      hPanels_[TAB_DASHBOARD - TAB_FIRST] = hPanel;

      // Stat cards (2x2 grid)
      struct StatCard { const wchar_t* label; const wchar_t* value; };
      StatCard cards[] = {
        {L"Top Windows", L"-"}, {L"Uptime", L"-"},
        {L"RPC Calls", L"-"},  {L"Control", L"-"},
      };
      for (int s = 0; s < 4; s++) {
        int col = s % 2, row = s / 2;
        int cx = layout_.margin + col * 220, cy = layout_.margin + row * 80;
        HWND hCard = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_NOTIFY,
            cx, cy, 210, 70, hPanel, nullptr, hInst_, nullptr);
        hStatCards_[s] = hCard;
        // Value label (large text, will be updated via setStatValue)
        CreateWindowExW(0, L"STATIC", cards[s].value,
            WS_VISIBLE | WS_CHILD,
            10, 8, 190, 30, hCard, nullptr, hInst_, nullptr);
        // Description label
        CreateWindowExW(0, L"STATIC", cards[s].label,
            WS_VISIBLE | WS_CHILD,
            10, 38, 190, 20, hCard, nullptr, hInst_, nullptr);
      }

      // Capture preview area (placeholder)
      CreateWindowExW(0, L"STATIC", L"Click Refresh to capture screen",
          WS_VISIBLE | WS_CHILD | SS_CENTER,
          layout_.margin, 180, 440, 160, hPanel, nullptr, hInst_, nullptr);

      // Events label
      CreateWindowExW(0, L"STATIC", L"Recent Events",
          WS_VISIBLE | WS_CHILD,
          layout_.margin, 350, 200, 24, hPanel, nullptr, hInst_, nullptr);

      // Events ListView
      int evLeft = layout_.margin;
      int evTop = 378;
      hEventsList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
          WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT,
          evLeft, evTop, 440, 120, hPanel, (HMENU)201, hInst_, nullptr);
      ListView_SetExtendedListViewStyle(hEventsList_, LVS_EX_FULLROWSELECT);
      LVCOLUMNW evCol = {};
      evCol.mask = LVCF_TEXT | LVCF_WIDTH;
      evCol.cx = 80; evCol.pszText = (LPWSTR)L"Time";    ListView_InsertColumn(hEventsList_, 0, &evCol);
      evCol.cx = 80; evCol.pszText = (LPWSTR)L"Level";   ListView_InsertColumn(hEventsList_, 1, &evCol);
      evCol.cx = 260; evCol.pszText = (LPWSTR)L"Message"; ListView_InsertColumn(hEventsList_, 2, &evCol);

      // Sample events
      const wchar_t* events[][3] = {
        {L"11:45:23", L"INFO", L"Daemon health check OK"},
        {L"11:45:18", L"INFO", L"window.listTop returned 373 windows"},
        {L"11:45:10", L"INFO", L"Client connected (pipe)"},
        {L"11:44:55", L"INFO", L"daemon.identity → uuid: 98e5b061"},
        {L"11:44:30", L"INFO", L"WinInspect Daemon v0.4.0 starting up"},
      };
      for (int e = 0; e < 5; e++) {
        LVITEMW item = {}; item.mask = LVIF_TEXT; item.iItem = e;
        for (int c = 0; c < 3; c++) {
          item.iSubItem = c;
          item.pszText = (LPWSTR)events[e][c];
          if (c == 0) ListView_InsertItem(hEventsList_, &item);
          else ListView_SetItemText(hEventsList_, e, c, (LPWSTR)events[e][c]);
        }
      }
    }

    // ── Capture tab ───────────────────────────────────────────────────
    {
      HWND hPanel = CreateWindowExW(0, L"STATIC", L"",
          WS_VISIBLE | WS_CHILD, 0, 0, 100, 100, hwnd_, nullptr, hInst_, nullptr);
      hPanels_[TAB_CAPTURE - TAB_FIRST] = hPanel;

      // Capture preview area (larger, left side)
      hCapturePreview_ = CreateWindowExW(0, L"STATIC", L"Capture preview",
          WS_VISIBLE | WS_CHILD | SS_CENTER,
          layout_.margin, layout_.margin, 400, 280, hPanel, nullptr, hInst_, nullptr);

      // Capture buttons
      int bx = 420, by = layout_.margin;
      int bw = 130, bh = 28;
      hCaptureBtn_ = CreateWindowExW(0, L"BUTTON", L"Capture Full Screen",
          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
          bx, by, bw, bh, hPanel, (HMENU)301, hInst_, nullptr);
      hCaptureRegionBtn_ = CreateWindowExW(0, L"BUTTON", L"Capture Region",
          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
          bx, by + 34, bw, bh, hPanel, (HMENU)302, hInst_, nullptr);
      hCaptureWindowBtn_ = CreateWindowExW(0, L"BUTTON", L"Capture Window",
          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
          bx, by + 68, bw, bh, hPanel, (HMENU)303, hInst_, nullptr);

      // Strategy selector
      int oy = by + 110;
      CreateWindowExW(0, L"STATIC", L"Strategy",
          WS_VISIBLE | WS_CHILD, bx, oy, bw, 20, hPanel, nullptr, hInst_, nullptr);
      hCaptureStrategy_ = CreateWindowExW(0, WC_COMBOBOXW, L"",
          WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
          bx, oy + 20, bw, 100, hPanel, nullptr, hInst_, nullptr);
      SendMessageW(hCaptureStrategy_, CB_ADDSTRING, 0, (LPARAM)L"Auto (best)");
      SendMessageW(hCaptureStrategy_, CB_ADDSTRING, 0, (LPARAM)L"DXGI");
      SendMessageW(hCaptureStrategy_, CB_ADDSTRING, 0, (LPARAM)L"WARP D3D11");
      SendMessageW(hCaptureStrategy_, CB_ADDSTRING, 0, (LPARAM)L"GDI BitBlt");
      SendMessageW(hCaptureStrategy_, CB_SETCURSEL, 0, 0);

      // Format selector
      CreateWindowExW(0, L"STATIC", L"Format",
          WS_VISIBLE | WS_CHILD, bx, oy + 55, bw, 20, hPanel, nullptr, hInst_, nullptr);
      hCaptureFormat_ = CreateWindowExW(0, WC_COMBOBOXW, L"",
          WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
          bx, oy + 75, bw, 100, hPanel, nullptr, hInst_, nullptr);
      SendMessageW(hCaptureFormat_, CB_ADDSTRING, 0, (LPARAM)L"PNG");
      SendMessageW(hCaptureFormat_, CB_ADDSTRING, 0, (LPARAM)L"JPEG");
      SendMessageW(hCaptureFormat_, CB_ADDSTRING, 0, (LPARAM)L"BMP");
      SendMessageW(hCaptureFormat_, CB_SETCURSEL, 0, 0);
    }

    // ── Input tab ─────────────────────────────────────────────────────
    {
      HWND hPanel = CreateWindowExW(0, L"STATIC", L"",
          WS_VISIBLE | WS_CHILD, 0, 0, 100, 100, hwnd_, nullptr, hInst_, nullptr);
      hPanels_[TAB_INPUT - TAB_FIRST] = hPanel;

      // Mouse pad section
      int mx = layout_.margin + 10, my = layout_.margin;
      const wchar_t* dirLabels[9] = {L"↖",L"↑",L"↗",L"←",L"●",L"→",L"↙",L"↓",L"↘"};
      for (int d = 0; d < 9; d++) {
        int col = d % 3, row = d / 3;
        hMouseBtns_[d] = CreateWindowExW(0, L"BUTTON", dirLabels[d],
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            mx + col * 50, my + row * 50, 48, 48,
            hPanel, (HMENU)(INT_PTR)(401 + d), hInst_, nullptr);
      }
      // Click buttons
      hLeftClickBtn_ = CreateWindowExW(0, L"BUTTON", L"Left Click",
          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
          mx, my + 170, 100, 28, hPanel, (HMENU)410, hInst_, nullptr);
      hRightClickBtn_ = CreateWindowExW(0, L"BUTTON", L"Right Click",
          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
          mx + 110, my + 170, 100, 28, hPanel, (HMENU)411, hInst_, nullptr);
      // Mouse position label
      CreateWindowExW(0, L"STATIC", L"Mouse: - , -",
          WS_VISIBLE | WS_CHILD, mx, my + 210, 220, 20,
          hPanel, nullptr, hInst_, nullptr);

      // Text input section
      int tx = 280;
      CreateWindowExW(0, L"STATIC", L"Text",
          WS_VISIBLE | WS_CHILD, tx, my, 60, 20, hPanel, nullptr, hInst_, nullptr);
      hInputText_ = CreateWindowExW(0, L"EDIT", L"",
          WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
          tx, my + 22, 200, 24, hPanel, nullptr, hInst_, nullptr);
      hTextSendBtn_ = CreateWindowExW(0, L"BUTTON", L"Send",
          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
          tx + 210, my + 22, 60, 24, hPanel, (HMENU)420, hInst_, nullptr);

      // Hotkey section
      CreateWindowExW(0, L"STATIC", L"Hotkey",
          WS_VISIBLE | WS_CHILD, tx, my + 60, 60, 20, hPanel, nullptr, hInst_, nullptr);
      hInputHotkey_ = CreateWindowExW(0, L"EDIT", L"",
          WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
          tx, my + 82, 200, 24, hPanel, nullptr, hInst_, nullptr);
      hHotkeySendBtn_ = CreateWindowExW(0, L"BUTTON", L"Send",
          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
          tx + 210, my + 82, 60, 24, hPanel, (HMENU)430, hInst_, nullptr);

      // Status area
      hInputStatus_ = CreateWindowExW(0, L"STATIC", L"Input ready",
          WS_VISIBLE | WS_CHILD, tx, my + 120, 300, 20, hPanel, nullptr, hInst_, nullptr);
    }

    // ── Sessions tab ──────────────────────────────────────────────────
    {
      HWND hPanel = CreateWindowExW(0, L"STATIC", L"",
          WS_VISIBLE | WS_CHILD, 0, 0, 100, 100, hwnd_, nullptr, hInst_, nullptr);
      hPanels_[TAB_SESSIONS - TAB_FIRST] = hPanel;

      // Recording bar
      CreateWindowExW(0, L"BUTTON", L"⏺ Start Recording",
          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
          layout_.margin, layout_.margin, 130, 28, hPanel, (HMENU)(INT_PTR)501, hInst_, nullptr);
      hSessStatus_ = CreateWindowExW(0, L"STATIC", L"No active recording",
          WS_VISIBLE | WS_CHILD, layout_.margin + 140, layout_.margin, 300, 28,
          hPanel, nullptr, hInst_, nullptr);

      // Session history
      CreateWindowExW(0, L"STATIC", L"Recorded Sessions",
          WS_VISIBLE | WS_CHILD, layout_.margin, 50, 200, 20, hPanel, nullptr, hInst_, nullptr);
      hSessList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
          WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT,
          layout_.margin, 74, 540, 200, hPanel, (HMENU)502, hInst_, nullptr);
      ListView_SetExtendedListViewStyle(hSessList_, LVS_EX_FULLROWSELECT);
      LVCOLUMNW sc = {}; sc.mask = LVCF_TEXT | LVCF_WIDTH;
      sc.cx = 100; sc.pszText = (LPWSTR)L"Name";    ListView_InsertColumn(hSessList_, 0, &sc);
      sc.cx = 80;  sc.pszText = (LPWSTR)L"Duration"; ListView_InsertColumn(hSessList_, 1, &sc);
      sc.cx = 70;  sc.pszText = (LPWSTR)L"Frames";   ListView_InsertColumn(hSessList_, 2, &sc);
      sc.cx = 80;  sc.pszText = (LPWSTR)L"Size";     ListView_InsertColumn(hSessList_, 3, &sc);
      sc.cx = 90;  sc.pszText = (LPWSTR)L"Date";     ListView_InsertColumn(hSessList_, 4, &sc);
    }

    // ── Events tab ────────────────────────────────────────────────────
    {
      HWND hPanel = CreateWindowExW(0, L"STATIC", L"",
          WS_VISIBLE | WS_CHILD, 0, 0, 100, 100, hwnd_, nullptr, hInst_, nullptr);
      hPanels_[TAB_EVENTS - TAB_FIRST] = hPanel;

      hEventLogList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
          WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT,
          layout_.margin, layout_.margin, 540, 350, hPanel, (HMENU)503, hInst_, nullptr);
      ListView_SetExtendedListViewStyle(hEventLogList_, LVS_EX_FULLROWSELECT);
      LVCOLUMNW ec = {}; ec.mask = LVCF_TEXT | LVCF_WIDTH;
      ec.cx = 80;  ec.pszText = (LPWSTR)L"Time";    ListView_InsertColumn(hEventLogList_, 0, &ec);
      ec.cx = 60;  ec.pszText = (LPWSTR)L"Level";   ListView_InsertColumn(hEventLogList_, 1, &ec);
      ec.cx = 380; ec.pszText = (LPWSTR)L"Message";  ListView_InsertColumn(hEventLogList_, 2, &ec);
    }

    // ── Metrics tab ───────────────────────────────────────────────────
    {
      HWND hPanel = CreateWindowExW(0, L"STATIC", L"",
          WS_VISIBLE | WS_CHILD, 0, 0, 100, 100, hwnd_, nullptr, hInst_, nullptr);
      hPanels_[TAB_METRICS - TAB_FIRST] = hPanel;

      // Stats cards for metrics
      struct MCard { const wchar_t* label; const wchar_t* value; };
      MCard mcards[] = {
        {L"Total RPC Calls", L"-"}, {L"Calls/min", L"-"},
        {L"Avg Latency", L"-"},     {L"Active Sessions", L"-"},
      };
      for (int s = 0; s < 4; s++) {
        int col = s % 2, row = s / 2;
        int cx = layout_.margin + col * 220, cy = layout_.margin + row * 80;
        hMetricCards_[s] = CreateWindowExW(0, L"STATIC", L"",
            WS_VISIBLE | WS_CHILD | SS_NOTIFY,
            cx, cy, 210, 70, hPanel, nullptr, hInst_, nullptr);
        CreateWindowExW(0, L"STATIC", mcards[s].value,
            WS_VISIBLE | WS_CHILD, 10, 8, 190, 30, hMetricCards_[s], nullptr, hInst_, nullptr);
        CreateWindowExW(0, L"STATIC", mcards[s].label,
            WS_VISIBLE | WS_CHILD, 10, 38, 190, 20, hMetricCards_[s], nullptr, hInst_, nullptr);
      }

      // Method breakdown
      int by = 180;
      CreateWindowExW(0, L"STATIC", L"Method Breakdown",
          WS_VISIBLE | WS_CHILD, layout_.margin, by, 200, 20, hPanel, nullptr, hInst_, nullptr);
      hMetricsList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
          WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT,
          layout_.margin, by + 24, 440, 150, hPanel, (HMENU)504, hInst_, nullptr);
      ListView_SetExtendedListViewStyle(hMetricsList_, LVS_EX_FULLROWSELECT);
      LVCOLUMNW mc = {}; mc.mask = LVCF_TEXT | LVCF_WIDTH;
      mc.cx = 180; mc.pszText = (LPWSTR)L"Method"; ListView_InsertColumn(hMetricsList_, 0, &mc);
      mc.cx = 80;  mc.pszText = (LPWSTR)L"Calls";  ListView_InsertColumn(hMetricsList_, 1, &mc);
      mc.cx = 80;  mc.pszText = (LPWSTR)L"Avg (ms)";ListView_InsertColumn(hMetricsList_, 2, &mc);
    }

    // ── Processes tab ─────────────────────────────────────────────────
    {
      HWND hPanel = CreateWindowExW(0, L"STATIC", L"",
          WS_VISIBLE | WS_CHILD, 0, 0, 100, 100, hwnd_, nullptr, hInst_, nullptr);
      hPanels_[TAB_PROCESSES - TAB_FIRST] = hPanel;

      hProcList_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
          WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT,
          layout_.margin, layout_.toolbar_h, 540, 350, hPanel, (HMENU)505, hInst_, nullptr);
      ListView_SetExtendedListViewStyle(hProcList_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
      LVCOLUMNW pc = {}; pc.mask = LVCF_TEXT | LVCF_WIDTH;
      pc.cx = 70;  pc.pszText = (LPWSTR)L"PID";     ListView_InsertColumn(hProcList_, 0, &pc);
      pc.cx = 200; pc.pszText = (LPWSTR)L"Name";    ListView_InsertColumn(hProcList_, 1, &pc);
      pc.cx = 80;  pc.pszText = (LPWSTR)L"Memory";  ListView_InsertColumn(hProcList_, 2, &pc);

      hProcKillBtn_ = CreateWindowExW(0, L"BUTTON", L"Kill Process",
          WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
          layout_.margin, layout_.margin, layout_.button_w + 20, layout_.button_h,
          hPanel, (HMENU)(INT_PTR)ID_KILL_BTN, hInst_, nullptr);
    }

    // Show Windows tab, hide others
    switchTab(active_tab_);
    applyTheme();
  }

  void onSize() {
    RECT r; GetClientRect(hwnd_, &r);
    int cw = r.right - r.left; int ch = r.bottom - r.top;
    // Toolbar row
    int tbH = 30;
    if (hConnectBtn_) MoveWindow(hConnectBtn_, layout_.margin, (tbH - 26) / 2, 70, 26, TRUE);
    if (hSearchEdit_) MoveWindow(hSearchEdit_, layout_.margin + 75, (tbH - 24) / 2, 120, 24, TRUE);

    // Sidebar (below toolbar)
    MoveWindow(hSidebar_, 0, tbH, layout_.sidebar_w, ch - tbH, TRUE);
    for (int i = 0; i < (TAB_COUNT - TAB_FIRST); i++) {
      HWND btn = GetDlgItem(hwnd_, TAB_FIRST + i);
      if (btn) MoveWindow(btn, 1, tbH + i * layout_.sidebar_btn_h, layout_.sidebar_w - 2, layout_.sidebar_btn_h, TRUE);
    }

    // Content area (right of sidebar)
    int cx = layout_.sidebar_w;
    int cw_content = cw - cx;
    int contentTop = 0;
    for (auto& h : hPanels_) {
      if (h) MoveWindow(h, cx, 0, cw_content, ch, TRUE);
    }

    // Windows tab content layout
    int w = cw_content; int tb = layout_.toolbar_h;
    MoveWindow(hRefreshBtn_, layout_.margin, layout_.margin, layout_.button_w, layout_.button_h, TRUE);
    if (cw_content > layout_.margin * 2 + layout_.button_w + 100) {
      int br = layout_.margin * 2 + layout_.button_w;
      MoveWindow(hStatus_, br, layout_.margin, w - br - layout_.margin, layout_.button_h, TRUE);
    }
    MoveWindow(hTree_, 0, tb, layout_.split_w, ch - tb, TRUE);
    MoveWindow(hList_, layout_.split_w, tb, w - layout_.split_w, ch - tb, TRUE);
  }

  void onMinMaxInfo(MINMAXINFO* mmi) {
    mmi->ptMinTrackSize.x = layout_.min_w;
    mmi->ptMinTrackSize.y = layout_.min_h;
  }

  void onDpiChanged(WPARAM wParam, LPARAM lParam) {
    dpi_ = HIWORD(wParam); layout_.scale(dpi_);
    RECT* r = (RECT*)lParam;
    SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    applyTheme(); onSize();
  }

  // ── Daemon lifecycle ─────────────────────────────────────────────────

  void onStartDaemon() {
    // Launch wininspectd.exe next to the GUI binary
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring dir = modulePath;
    auto pos = dir.find_last_of(L"\\");
    if (pos != std::wstring::npos) dir = dir.substr(0, pos + 1);
    std::wstring daemonPath = dir + L"wininspectd.exe";
    std::wstring cmdLine = L"\"" + daemonPath + L"\" --headless --no-auth";
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    if (CreateProcessW(daemonPath.c_str(), &cmdLine[0], nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      setStatus(L"Starting daemon...");
      // Hide start button, show indicator
      ShowWindow(hStartDaemonBtn_, SW_HIDE);
      ShowWindow(hDaemonIndicator_, SW_SHOW);
      SetWindowTextW(hDaemonIndicator_, L"● Starting...");
      // Retry connection
      for (int i = 0; i < 20; i++) {
        Sleep(500);
        if (transport_->connected()) {
          setStatus(L"Daemon started");
          pollDaemonStatus();
          refresh();
          return;
        }
      }
      setStatus(L"Daemon started but not responding");
    } else {
      setStatus(L"Failed to start daemon");
    }
  }

  void pollDaemonStatus() {
    if (!transport_->connected()) {
      ShowWindow(hDaemonIndicator_, SW_HIDE);
      ShowWindow(hStartDaemonBtn_, SW_SHOW);
      SetWindowTextW(hDaemonIndicator_, L"● Daemon: stopped");
      daemon_indicator_color_ = RGB(0xc6, 0x28, 0x28); // red
      return;
    }
    ShowWindow(hDaemonIndicator_, SW_SHOW);
    ShowWindow(hStartDaemonBtn_, SW_HIDE);
    // Query controller status for the indicator color
    using namespace wininspect::json;
    Object req;
    req["id"] = std::string("gui-daemon");
    req["method"] = std::string("control.status");
    req["params"] = Object{};
    auto raw = transport_->request(dumps(req));
    auto resp = parse(raw);
    if (resp.is_obj()) {
      auto& obj = resp.as_obj();
      auto ok = obj.find("ok");
      if (ok != obj.end() && ok->second.is_bool() && ok->second.as_bool()) {
        auto result = obj.find("result");
        if (result != obj.end() && result->second.is_obj()) {
          auto ctrl = result->second.as_obj().find("current_controller");
          if (ctrl != result->second.as_obj().end() && ctrl->second.is_str()) {
            std::string who = ctrl->second.as_str();
            if (who == "Human" || who == "human") {
              SetWindowTextW(hDaemonIndicator_, L"● You're in control");
              daemon_indicator_color_ = RGB(0x2e, 0x7d, 0x32);
            }
            else if (who == "Agent" || who == "agent") {
              SetWindowTextW(hDaemonIndicator_, L"● Agent is working");
              daemon_indicator_color_ = RGB(0x21, 0x96, 0xf3);
            }
            else if (who == "Script" || who == "automation") {
              SetWindowTextW(hDaemonIndicator_, L"● Automation active");
              daemon_indicator_color_ = RGB(0xff, 0x98, 0x00);
            }
            InvalidateRect(hDaemonIndicator_, nullptr, TRUE);
            return;
          }
        }
      }
    }
    SetWindowTextW(hDaemonIndicator_, L"● Daemon running");
    daemon_indicator_color_ = RGB(0x2e, 0x7d, 0x32);
    InvalidateRect(hDaemonIndicator_, nullptr, TRUE);
  }

  // ── Commands ──────────────────────────────────────────────────────────

  void onHighlight() {
    HTREEITEM sel = TreeView_GetSelection(hTree_);
    if (!sel) { setStatus(L"No window selected"); return; }
    TVITEMW item = {0};
    item.mask = TVIF_PARAM;
    item.hItem = sel;
    TreeView_GetItem(hTree_, &item);
    size_t idx = (size_t)item.lParam;
    if (idx >= hwnd_storage_.size()) { setStatus(L"Invalid selection"); return; }
    // Build and send window.highlight RPC
    {
      using namespace wininspect::json;
      Object req;
      req["id"] = std::string("gui-hl");
      req["method"] = std::string("window.highlight");
      Object p; p["hwnd"] = hwnd_storage_[idx];
      req["params"] = p;
      transport_->request(dumps(req));
    }
    setStatus((L"Highlighted: " + std::wstring(hwnd_storage_[idx].begin(), hwnd_storage_[idx].end())).c_str());
  }

  // ── Process kill ─────────────────────────────────────────────────────

  void onKillProcess() {
    int sel = ListView_GetNextItem(hProcList_, -1, LVNI_SELECTED);
    if (sel < 0) { setStatus(L"No process selected"); return; }
    wchar_t buf[32] = {};
    ListView_GetItemText(hProcList_, sel, 0, buf, 32);
    std::wstring ws(buf);
    std::string pid(ws.begin(), ws.end());
    if (MessageBoxW(hwnd_, (L"Kill process " + ws + L"? Use --force to confirm.").c_str(),
                     L"Confirm Kill", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    using namespace wininspect::json;
    Object req;
    req["id"] = std::string("gui-kill");
    req["method"] = std::string("process.kill");
    Object p; p["pid"] = std::stod(pid); p["force"] = true;
    req["params"] = p;
    transport_->request(dumps(req));
    setStatus(L"Killed PID " + ws);
    refresh();
  }

  // ── Data refresh ──────────────────────────────────────────────────────

  void refresh() { refreshImpl(false); }
  void refreshAsync() {
    if (refresh_running_.test_and_set(std::memory_order_acquire)) return;
    std::thread([this]() { refreshImpl(true); refresh_running_.clear(std::memory_order_release); }).detach();
  }

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

  void refreshProcessList() {
    using namespace wininspect::json;
    Object req;
    req["id"] = std::string("gui-proc");
    req["method"] = std::string("process.list");
    req["params"] = Object{};
    auto raw = transport_->request(dumps(req));
    auto resp = parse(raw);
    if (!resp.is_obj()) return;
    auto& obj = resp.as_obj();
    auto ok = obj.find("ok");
    if (ok == obj.end() || !ok->second.as_bool()) return;
    auto result = obj.find("result");
    if (result == obj.end() || !result->second.is_obj()) return;
    auto procs = result->second.as_obj().find("processes");
    if (procs == result->second.as_obj().end() || !procs->second.is_arr()) return;
    ListView_DeleteAllItems(hProcList_);
    int i = 0;
    for (auto& p : procs->second.as_arr()) {
      if (!p.is_obj()) continue;
      auto& po = p.as_obj();
      auto pid = po.find("pid");
      auto name = po.find("name");
      auto mem = po.find("memory_kb");
      if (pid == po.end() || name == po.end()) continue;
      LVITEMW item = {}; item.mask = LVIF_TEXT; item.iItem = i;
      std::wstring ws_pid = std::to_wstring((int)pid->second.as_num());
      item.pszText = (LPWSTR)ws_pid.c_str();
      ListView_InsertItem(hProcList_, &item);
      if (name->second.is_str()) {
        std::wstring ws_name(name->second.as_str().begin(), name->second.as_str().end());
        ListView_SetItemText(hProcList_, i, 1, (LPWSTR)ws_name.c_str());
      }
      if (mem != po.end()) {
        std::wstring ws_mem = std::to_wstring((int)mem->second.as_num()) + L" KB";
        ListView_SetItemText(hProcList_, i, 2, (LPWSTR)ws_mem.c_str());
      }
      i++;
    }
  }

  void setStatus(const std::wstring& text) {
    std::wstring dot = transport_->connected() ? L"● " : L"● ";
    if (GetCurrentThreadId() != main_thread_id_) {
      PostMessageW(hwnd_, WM_APP_SET_STATUS, 0, (LPARAM)_wcsdup(text.c_str()));
      return;
    }
    SetWindowTextW(hStatus_, (dot + text).c_str());
  }

  void addNode(HTREEITEM parent, const Node& n) {
    TVINSERTSTRUCTW tvi = {};
    tvi.hParent = parent; tvi.hInsertAfter = TVI_LAST;
    tvi.item.mask = TVIF_TEXT | TVIF_PARAM;
    std::wstring wlabel(n.label.begin(), n.label.end());
    tvi.item.pszText = (LPWSTR)wlabel.c_str();
    hwnd_storage_.push_back(n.hwnd);
    tvi.item.lParam = (LPARAM)(hwnd_storage_.size() - 1);
    HTREEITEM hItem = TreeView_InsertItem(hTree_, &tvi);
    for (const auto& child : n.children) addNode(hItem, child);
  }

  void onNotify(LPARAM lParam) {
    LPNMHDR nm = (LPNMHDR)lParam;
    if (nm->hwndFrom == hTree_ && nm->code == NM_CUSTOMDRAW) return onTreeCustomDraw(lParam);
    if ((nm->hwndFrom == hList_ || nm->hwndFrom == hEventsList_ ||
         nm->hwndFrom == hSessList_ || nm->hwndFrom == hMetricsList_ ||
         nm->hwndFrom == hProcList_ || nm->hwndFrom == hEventLogList_) &&
        nm->code == NM_CUSTOMDRAW) return onCustomDraw(lParam);
    if (nm->code == TVN_SELCHANGEDW) {
      LPNMTREEVIEWW nmtv = (LPNMTREEVIEWW)lParam;
      size_t idx = (size_t)nmtv->itemNew.lParam;
      if (idx < hwnd_storage_.size()) {
        vm_->select_hwnd(hwnd_storage_[idx]);
        ListView_DeleteAllItems(hList_);
        int i = 0;
        for (const auto& p : vm_->props()) {
          LVITEMW item = {}; item.mask = LVIF_TEXT; item.iItem = i;
          std::wstring wk(p.key.begin(), p.key.end()); item.pszText = (LPWSTR)wk.c_str();
          ListView_InsertItem(hList_, &item);
          std::wstring wv(p.value.begin(), p.value.end());
          ListView_SetItemText(hList_, i, 1, (LPWSTR)wv.c_str());
          i++;
        }
      }
    }
  }
};

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
  // Enable per-monitor DPI awareness (Windows 10+)
  HMODULE hShcore = LoadLibraryW(L"shcore.dll");
  if (hShcore) {
    typedef HRESULT(WINAPI* SetProcessDpiAwarenessFunc)(int);
    auto setDpiAwareness = (SetProcessDpiAwarenessFunc)GetProcAddress(hShcore, "SetProcessDpiAwareness");
    if (setDpiAwareness) setDpiAwareness(2); // PROCESS_PER_MONITOR_DPI_AWARE
    FreeLibrary(hShcore);
  } else {
    SetProcessDPIAware(); // Fallback for older Windows
  }

  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
  InitCommonControlsEx(&icc);
  WinInspectWindow win;
  if (!win.init(hInst)) return 1;
  win.show(nCmdShow);
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (!win.translate_accel(&msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  return 0;
}
#else
int main() { return 0; }
#endif
