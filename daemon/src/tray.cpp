#include "tray.hpp"
#include <shellapi.h>

namespace wininspectd {

TrayManager::TrayManager(OnExitCallback onExit) : onExit_(onExit) {}

TrayManager::~TrayManager() { stop(); }

bool TrayManager::init(HINSTANCE hInstance) {
  hInst_ = hInstance;

  WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
  wc.lpfnWndProc = windowProc;
  wc.hInstance = hInst_;
  wc.lpszClassName = L"WinInspectTrayWindow";

  if (!RegisterClassExW(&wc))
    return false;

  hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"WinInspect Daemon Tray", 0, 0,
                          0, 0, 0, HWND_MESSAGE, nullptr, hInst_, this);
  if (!hwnd_)
    return false;

  NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
  nid.hWnd = hwnd_;
  nid.uID = 1;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = WM_TRAYICON;
  nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wcscpy_s(nid.szTip, L"WinInspect Daemon");

  if (!Shell_NotifyIconW(NIM_ADD, &nid))
    return false;

  return true;
}

void TrayManager::run() {
  running_ = true;
  MSG msg;
  while (running_ && GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

void TrayManager::stop() {
  if (hwnd_) {
    NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
    nid.hWnd = hwnd_;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  running_ = false;
}

LRESULT CALLBACK TrayManager::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                         LPARAM lParam) {
  TrayManager *self = nullptr;
  if (uMsg == WM_NCCREATE) {
    CREATESTRUCT *cs = reinterpret_cast<CREATESTRUCT *>(lParam);
    self = reinterpret_cast<TrayManager *>(cs->lpCreateParams);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self =
        reinterpret_cast<TrayManager *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }

  if (self) {
    if (uMsg == WM_TRAYICON) {
      self->handleTrayMessage(lParam);
      return 0;
    } else if (uMsg == WM_COMMAND) {
      if (LOWORD(wParam) == ID_TRAY_EXIT) {
        if (self->onExit_)
          self->onExit_();
        self->stop();
      } else if (LOWORD(wParam) == ID_TRAY_ABOUT) {
        MessageBoxW(hwnd, L"WinInspect Daemon\nMonitoring windows with style.", L"About", MB_OK | MB_ICONINFORMATION);
      }
      return 0;
    }
  }

  return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void TrayManager::handleTrayMessage(LPARAM lParam) {
  if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
    showContextMenu();
  }
}

void TrayManager::showContextMenu() {
  HMENU hMenu = CreatePopupMenu();
  if (hMenu) {
    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_ABOUT, L"About");
    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd_,
                   nullptr);
    DestroyMenu(hMenu);
  }
}

} // namespace wininspectd
