// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "tray.hpp"
#include "resource.h"
#include "wininspect/types.hpp"
#include <shellapi.h>
#include <string>
#include <chrono>
#include <ctime>

namespace wininspectd {

  TrayManager::TrayManager(OnExitCallback onExit) : onExit_(onExit) {}

  TrayManager::~TrayManager()
  {
    stop();
  }

  bool TrayManager::init(HINSTANCE hInstance)
  {
    hInst_ = hInstance;

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = hInst_;
    wc.hIcon = LoadIcon(hInst_, MAKEINTRESOURCE(IDI_WININSPECT));
    wc.lpszClassName = L"WinInspectTrayWindow";

    if (!RegisterClassExW(&wc))
      return false;

    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"WinInspect Daemon Tray", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, hInst_, this);
    if (!hwnd_)
      return false;

    // Use Strix icon from resources
    HICON hIcon = LoadIcon(hInst_, MAKEINTRESOURCE(IDI_WININSPECT));

    NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = hIcon ? hIcon : LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"WinInspect Daemon starting...");

    if (!Shell_NotifyIconW(NIM_ADD, &nid))
      return false;

    // Start health check timer (10 second interval)
    SetTimer(hwnd_, HEALTH_TIMER_ID, 10000, nullptr);

    return true;
  }

  void TrayManager::run()
  {
    running_ = true;
    MSG msg;
    while (running_ && GetMessage(&msg, nullptr, 0, 0)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  void TrayManager::stop()
  {
    if (hwnd_) {
      NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
      nid.hWnd = hwnd_;
      nid.uID = 1;
      KillTimer(hwnd_, HEALTH_TIMER_ID);
      Shell_NotifyIconW(NIM_DELETE, &nid);
      DestroyWindow(hwnd_);
      hwnd_ = nullptr;
    }
    running_ = false;
  }

  LRESULT CALLBACK TrayManager::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
  {
    TrayManager* self = nullptr;
    if (uMsg == WM_NCCREATE) {
      CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
      self = reinterpret_cast<TrayManager*>(cs->lpCreateParams);
      SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else {
      self = reinterpret_cast<TrayManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self) {
      if (uMsg == WM_TRAYICON) {
        self->handleTrayMessage(lParam);
        return 0;
      }
      else if (uMsg == WM_TIMER && wParam == HEALTH_TIMER_ID) {
        self->refreshTooltip();
        if (self->indicator_cb_)
          self->indicator_cb_();
        return 0;
      }
      else if (uMsg == WM_REFRESH_TRAY) {
        self->refreshTooltip();
        return 0;
      }
      else if (uMsg == WM_TRAY_BADGE_UPDATE) {
        // Update tray icon with colored badge (wParam = ControllerType, lParam = COLORREF)
        auto who = (wininspect::ControllerType)wParam;
        COLORREF badgeColor = (COLORREF)lParam;
        self->updateBadge(who, badgeColor);
        return 0;
      }
      else if (uMsg == WM_UPDATE_AVAILABLE) {
        // Update notification from background thread
        if (self->update_state_.available) {
          std::string msg = "Version " + self->update_state_.latest_version + " is available!";
          self->show_notification("WinInspect Update", msg);
        }
        return 0;
      }
      else if (uMsg == WM_COMMAND) {
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
          if (self->onExit_)
            self->onExit_();
          self->stop();
        }
        else if (LOWORD(wParam) == ID_TRAY_ABOUT) {
          MessageBoxW(hwnd, L"WinInspect Daemon\nMonitoring windows with style.", L"About",
                      MB_OK | MB_ICONINFORMATION);
        }
        else if (LOWORD(wParam) == ID_TRAY_UPDATE) {
          std::string url = "https://github.com/SemperSupra/WinInspect/releases/tag/v" +
                            self->update_state_.latest_version;
          std::wstring wurl(url.begin(), url.end());
          ShellExecuteW(hwnd, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
      }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  }

  void TrayManager::updateBadge(wininspect::ControllerType who, COLORREF color)
  {
    if (!hwnd_) return;

    // Load the base Strix icon
    HICON hBaseIcon = LoadIcon(hInst_, MAKEINTRESOURCE(IDI_WININSPECT));
    if (!hBaseIcon) return;

    // Get icon info to access the bitmap
    ICONINFO ii = {};
    if (!GetIconInfo(hBaseIcon, &ii)) {
      DestroyIcon(hBaseIcon);
      return;
    }

    // Create a DC to draw on
    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc) {
      DeleteObject(ii.hbmColor);
      DeleteObject(ii.hbmMask);
      DestroyIcon(hBaseIcon);
      return;
    }

    // Get icon dimensions
    BITMAP bm = {};
    GetObject(ii.hbmColor, sizeof(bm), &bm);
    int w = bm.bmWidth;
    int h = bm.bmHeight;

    // Select the color bitmap into the DC
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdc, ii.hbmColor);

    // Draw badge circle (bottom-right corner, ~1/4 of icon size)
    if (color != 0 && who != wininspect::ControllerType::None) {
      int dot_r = w / 5;           // radius
      int cx = w - dot_r - 1;       // center x
      int cy = h - dot_r - 1;       // center y

      HBRUSH hBrush = CreateSolidBrush(color);
      HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xff, 0xff, 0xff));

      HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
      HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

      Ellipse(hdc, cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r);

      SelectObject(hdc, hOldBrush);
      SelectObject(hdc, hOldPen);
      DeleteObject(hBrush);
      DeleteObject(hPen);
    }

    // Create the new icon
    HICON hNewIcon = CreateIconIndirect(&ii);

    // Clean up
    SelectObject(hdc, hOldBmp);
    DeleteDC(hdc);
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    DestroyIcon(hBaseIcon);

    // Update the tray icon
    if (hNewIcon) {
      NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
      nid.hWnd = hwnd_;
      nid.uID = 1;
      nid.uFlags = NIF_ICON;
      nid.hIcon = hNewIcon;
      Shell_NotifyIconW(NIM_MODIFY, &nid);
      DestroyIcon(hNewIcon);
    }
  }

  void TrayManager::handleTrayMessage(LPARAM lParam)
  {
    if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
      showContextMenu();
    }
  }

  void TrayManager::refreshTooltip()
  {
    NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;

    // Query current controller and connections
    if (controller_cb_) last_controller_ = controller_cb_();
    if (conn_cb_) last_conn_count_ = conn_cb_();
    bool ok = !health_flag_ || health_flag_->load();

    // Build tooltip: 3 lines with friendly control messaging
    std::wstring tip = L"WinInspect";
    std::string ctrl_msg;
    if (last_controller_ == "Human" || last_controller_ == "human")
      ctrl_msg = "You're in control";
    else if (last_controller_ == "Agent" || last_controller_ == "agent")
      ctrl_msg = "Your agent is working — you can take control at any time";
    else if (last_controller_ == "Script" || last_controller_ == "automation")
      ctrl_msg = "Automation is active — you can take control at any time";
    else
      ctrl_msg = last_controller_;
    tip += L"\n" + std::wstring(ctrl_msg.begin(), ctrl_msg.end());
    tip += L" · " + std::to_wstring(last_conn_count_) + L" connection" +
           (last_conn_count_ == 1 ? L"" : L"s");
    if (!ok)
      tip += L"\n⚠ Can't reach the daemon — is it running?";

    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
  }

  void TrayManager::showContextMenu()
  {
    HMENU hMenu = CreatePopupMenu();
    if (hMenu) {
      UINT pos = 0;

      // Status info (disabled)
      std::wstring statusLine;
      if (last_controller_ == "Human" || last_controller_ == "human")
        statusLine = L"You're in control";
      else if (last_controller_ == "Agent" || last_controller_ == "agent")
        statusLine = L"Your agent is working";
      else if (last_controller_ == "Script" || last_controller_ == "automation")
        statusLine = L"Automation is active";
      else
        statusLine = L"Status: " + std::wstring(last_controller_.begin(), last_controller_.end());
      statusLine += L" · " + std::to_wstring(last_conn_count_) + L" conn";
      InsertMenuW(hMenu, pos, MF_BYPOSITION | MF_GRAYED, 0, statusLine.c_str());
      pos++;
      InsertMenuW(hMenu, pos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
      pos++;

      if (update_state_.available) {
        std::wstring updateItem =
            L"Update Available: v" +
            std::wstring(update_state_.latest_version.begin(), update_state_.latest_version.end());
        InsertMenuW(hMenu, pos, MF_BYPOSITION | MF_STRING, ID_TRAY_UPDATE, updateItem.c_str());
        pos++;
        InsertMenuW(hMenu, pos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        pos++;
      }
      InsertMenuW(hMenu, pos, MF_BYPOSITION | MF_STRING, ID_TRAY_ABOUT, L"About");
      pos++;
      InsertMenuW(hMenu, pos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
      pos++;
      InsertMenuW(hMenu, pos, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Exit");

      POINT pt;
      GetCursorPos(&pt);
      SetForegroundWindow(hwnd_);
      TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
      DestroyMenu(hMenu);
    }
  }

  void TrayManager::show_notification(const std::string& title, const std::string& message,
                                      bool is_warning)
  {
    if (!hwnd_)
      return;
    NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = is_warning ? NIIF_WARNING : NIIF_INFO;
    nid.uTimeout = 5000; // 5 seconds

    // Convert strings to wide
    std::wstring wtitle(title.begin(), title.end());
    std::wstring wmsg(message.begin(), message.end());
    wcscpy_s(nid.szInfoTitle, wtitle.c_str());
    wcscpy_s(nid.szInfo, wmsg.c_str());

    Shell_NotifyIconW(NIM_MODIFY, &nid);
  }

} // namespace wininspectd
