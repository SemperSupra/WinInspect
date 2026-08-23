#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// Optional control indicators — visual and audible cues showing
// whether the daemon is under human or agent control.
// All indicators are optional and configurable.
// Portable across Windows 10/11 and Wine 10/11/12.

#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <functional>
#include <string>

#include "wininspect/types.hpp"

namespace wininspectd {

  struct IndicatorConfig
  {
    bool tray_badge = true; // Tray icon overlay — low intrusion, ON by default
    bool cursor = false;    // System cursor swap — low intrusion, OFF by default
    bool border = false;    // Screen border overlay — low intrusion, OFF by default
    bool audio = false;     // Audio beep on handoff — medium intrusion, OFF by default

    void load_from_env();
    void load_from_config(const std::string& path);
    void save_to_config(const std::string& path) const;
  };

  class IndicatorManager
  {
  public:
    IndicatorManager();
    ~IndicatorManager();

    bool init(HINSTANCE hInst, HWND hTrayWnd);
    void update(wininspect::ControllerType who);
    void set_config(const IndicatorConfig& cfg);
    IndicatorConfig& config() { return config_; }

    /// Check if cursor was modified and needs restoration
    void restore_cursor();

  private:
    IndicatorConfig config_;
    HINSTANCE hInst_ = nullptr;
    HWND hTrayWnd_ = nullptr;
    wininspect::ControllerType last_controller_ = wininspect::ControllerType::None;
    HCURSOR orig_cursor_ = nullptr;
    bool cursor_active_ = false;

    // Border overlay window
    HWND hBorderWnd_ = nullptr;
    void createBorderWindow();
    void updateBorder(wininspect::ControllerType who);

    // Cursor
    void updateCursor(wininspect::ControllerType who);

    // Audio
    void playHandoffSound(wininspect::ControllerType who);

    static LRESULT CALLBACK borderWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
  };

} // namespace wininspectd
#endif
