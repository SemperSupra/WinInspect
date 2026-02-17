#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <functional>
#include <windows.h>

namespace wininspectd {

class TrayManager {
public:
  using OnExitCallback = std::function<void()>;

  TrayManager(OnExitCallback onExit);
  ~TrayManager();

  bool init(HINSTANCE hInstance);
  void run();
  void stop();

private:
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                     LPARAM lParam);
  void handleTrayMessage(LPARAM lParam);
  void showContextMenu();

  HWND hwnd_ = nullptr;
  HINSTANCE hInst_ = nullptr;
  OnExitCallback onExit_;
  bool running_ = false;

  static constexpr UINT WM_TRAYICON = WM_USER + 1;
  static constexpr UINT ID_TRAY_EXIT = 1001;
  static constexpr UINT ID_TRAY_ABOUT = 1002;
};

} // namespace wininspectd
#endif
