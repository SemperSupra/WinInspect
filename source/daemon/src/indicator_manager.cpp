// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// Control indicator manager — optional visual/audible cues.
// All indicators are configurable and portable.

#include "indicator_manager.hpp"
#include "resource.h"
#include "wininspect/logger.hpp"

#ifndef OCR_NORMAL
#define OCR_NORMAL 32512
#endif

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace wininspectd {

  // ── Controller colors — consistent across all indicators ─────────────────
  // Three distinct controller types with distinct colors:
  //   Human (green)  ─ the user, always wins
  //   Agent (blue)   ─ MCP-connected AI assistant (#228, #239)
  //   Automation (orange) ─ scripts, SDK clients, API consumers
  static constexpr COLORREF COL_NONE        = RGB(0x00, 0x00, 0x00);
  static constexpr COLORREF COL_HUMAN       = RGB(0x4c, 0xaf, 0x50);  // green
  static constexpr COLORREF COL_AGENT       = RGB(0x21, 0x96, 0xf3);  // blue
  static constexpr COLORREF COL_AUTOMATION  = RGB(0xff, 0x98, 0x00);  // orange

  static COLORREF controller_color(wininspect::ControllerType who)
  {
    switch (who) {
      case wininspect::ControllerType::Human:       return COL_HUMAN;
      case wininspect::ControllerType::Agent:       return COL_AGENT;
      case wininspect::ControllerType::Script:      return COL_AUTOMATION;
      default: return COL_NONE;
    }
  }

  // ── Config ──────────────────────────────────────────────────────────────

  void IndicatorConfig::load_from_env()
  {
    auto e = [](const char* name) -> std::string {
      char* v = std::getenv(name);
      return v ? std::string(v) : "";
    };

    auto v = e("WININSPECT_INDICATOR_TRAY_BADGE");
    if (!v.empty())
      tray_badge = (v == "1" || v == "true");

    v = e("WININSPECT_INDICATOR_CURSOR");
    if (!v.empty())
      cursor = (v == "1" || v == "true");

    v = e("WININSPECT_INDICATOR_BORDER");
    if (!v.empty())
      border = (v == "1" || v == "true");

    v = e("WININSPECT_INDICATOR_AUDIO");
    if (!v.empty())
      audio = (v == "1" || v == "true");
  }

  // ── IndicatorManager ────────────────────────────────────────────────────

  IndicatorManager::IndicatorManager()
  {
    config_.load_from_env();
  }

  IndicatorManager::~IndicatorManager()
  {
    restore_cursor();
    if (hBorderWnd_) {
      DestroyWindow(hBorderWnd_);
      hBorderWnd_ = nullptr;
    }
  }

  bool IndicatorManager::init(HINSTANCE hInst, HWND hTrayWnd)
  {
    hInst_ = hInst;
    hTrayWnd_ = hTrayWnd;
    return true;
  }

  void IndicatorManager::set_config(const IndicatorConfig& cfg)
  {
    config_ = cfg;
    // Re-apply current state
    update(last_controller_);
  }

  void IndicatorManager::update(wininspect::ControllerType who)
  {
    if (who == last_controller_)
      return;

    auto prev = last_controller_;
    last_controller_ = who;

    // Audio on handoff (independent of current state)
    if (config_.audio)
      playHandoffSound(who);

    // Cursor
    if (config_.cursor)
      updateCursor(who);

    // Border
    if (config_.border)
      updateBorder(who);

    // Tray icon badge: send color to TrayManager via custom message (WM_USER + 4)
    if (config_.tray_badge && hTrayWnd_)
      PostMessageW(hTrayWnd_, WM_USER + 4, (WPARAM)who, controller_color(who));

    LOG_DEBUG("Indicator: controller changed " +
              std::string(wininspect::controller_type_str(prev)) + " -> " +
              std::string(wininspect::controller_type_str(who)));
  }

  void IndicatorManager::restore_cursor()
  {
    if (cursor_active_ && orig_cursor_) {
      SetSystemCursor(orig_cursor_, OCR_NORMAL);
      cursor_active_ = false;
      orig_cursor_ = nullptr;
    }
  }

  // ── Cursor ──────────────────────────────────────────────────────────────

  void IndicatorManager::updateCursor(wininspect::ControllerType who)
  {
    if (who == wininspect::ControllerType::Agent) {
      if (!cursor_active_) {
        // Save original cursor
        orig_cursor_ = CopyCursor(LoadCursor(nullptr, IDC_ARROW));
        // Load agent cursor from file (portable: works on both Windows and Wine)
        std::wstring curPath = L"assets\\brand\\ico\\strix-agent-cursor.cur";
        HCURSOR agentCur =
            (HCURSOR)LoadImageW(nullptr, curPath.c_str(), IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE);
        if (!agentCur) {
          // Try from executable directory
          wchar_t modulePath[MAX_PATH];
          GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
          std::wstring dir = modulePath;
          auto pos = dir.find_last_of(L"\\");
          if (pos != std::wstring::npos) {
            dir = dir.substr(0, pos + 1) + L"assets\\brand\\ico\\strix-agent-cursor.cur";
            agentCur =
                (HCURSOR)LoadImageW(nullptr, dir.c_str(), IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE);
          }
        }
        if (agentCur) {
          SetSystemCursor(agentCur, OCR_NORMAL);
          cursor_active_ = true;
        }
      }
    }
    else {
      restore_cursor();
    }
  }

  // ── Border Overlay ──────────────────────────────────────────────────────

  void IndicatorManager::createBorderWindow()
  {
    if (hBorderWnd_)
      return;

    const wchar_t CLASS_NAME[] = L"WinInspectBorderOverlay";

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = borderWndProc;
    wc.hInstance = hInst_;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    // Create a transparent topmost window that covers the entire screen
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    hBorderWnd_ =
        CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                        CLASS_NAME, L"", WS_POPUP, 0, 0, sw, sh, nullptr, nullptr, hInst_, this);

    if (hBorderWnd_) {
      SetLayeredWindowAttributes(hBorderWnd_, 0, 1, LWA_ALPHA);
      SetWindowPos(hBorderWnd_, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
    }
  }

  void IndicatorManager::updateBorder(wininspect::ControllerType who)
  {
    createBorderWindow();
    if (!hBorderWnd_)
      return;

    if (who == wininspect::ControllerType::None) {
      ShowWindow(hBorderWnd_, SW_HIDE);
      return;
    }

    // Store the controller type as window data for WM_PAINT
    SetWindowLongPtrW(hBorderWnd_, GWLP_USERDATA,
                      (LONG_PTR)(who == wininspect::ControllerType::Human ? 1 : 2));
    ShowWindow(hBorderWnd_, SW_SHOW);
    InvalidateRect(hBorderWnd_, nullptr, TRUE);
    UpdateWindow(hBorderWnd_);
  }

  LRESULT CALLBACK IndicatorManager::borderWndProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                                   LPARAM lParam)
  {
    if (uMsg == WM_NCCREATE) {
      auto cs = (CREATESTRUCT*)lParam;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
      return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    if (uMsg == WM_PAINT) {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);

      LONG_PTR state = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
      COLORREF color = COL_NONE;
      if (state == 1) color = COL_HUMAN;
      else if (state == 2) color = COL_AGENT;  // MCP AI: blue
      else if (state >= 3) color = COL_AUTOMATION;  // Script/Automation: orange

      // Draw 3px border
      RECT r;
      GetClientRect(hwnd, &r);
      HBRUSH brush = CreateSolidBrush(color);
      FrameRect(hdc, &r, brush);
      // Wider border: draw 3 frames
      for (int i = 1; i <= 2; i++) {
        r.left += 1;
        r.top += 1;
        r.right -= 1;
        r.bottom -= 1;
        FrameRect(hdc, &r, brush);
      }
      DeleteObject(brush);

      EndPaint(hwnd, &ps);
      return 0;
    }

    if (uMsg == WM_ERASEBKGND)
      return 1; // Prevent flicker

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  }

  // ── Audio ───────────────────────────────────────────────────────────────

  void IndicatorManager::playHandoffSound(wininspect::ControllerType who)
  {
    if (who == wininspect::ControllerType::Human)
      Beep(660, 200); // Higher pitch — human
    else if (who == wininspect::ControllerType::Agent)
      Beep(440, 200); // Lower pitch — agent
  }

} // namespace wininspectd
