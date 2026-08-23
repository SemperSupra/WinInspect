#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#ifdef _WIN32
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <windows.h>
#include "wininspect/types.hpp"

namespace wininspectd {

  class TrayManager
  {
  public:
    using OnExitCallback = std::function<void()>;

    TrayManager(OnExitCallback onExit);
    ~TrayManager();

    /// Shared state for update notifications.
    struct UpdateState
    {
      std::string latest_version;
      std::string release_notes;
      bool available = false;
    };

    bool init(HINSTANCE hInstance);
    void run();
    void stop();

    /// Show a balloon notification from the tray icon.
    void show_notification(const std::string& title, const std::string& message,
                           bool is_warning = false);

    /// Get the hidden tray window handle (for PostMessage from other threads).
    HWND get_hwnd() const { return hwnd_; }

    /// Set shared update state (called from update check thread).
    void set_update_state(const UpdateState& s) { update_state_ = s; }

    /// Set daemon health status (shared atomic from daemon's cleanup thread).
    void set_health_flag(std::shared_ptr<std::atomic<bool>> flag) { health_flag_ = flag; }

    /// Set callbacks for dynamic status display in tray tooltip.
    using ControllerCallback = std::function<std::string()>;
    using ConnectionCallback = std::function<int()>;
    void set_status_callbacks(ControllerCallback cc, ConnectionCallback connc)
    {
      controller_cb_ = cc;
      conn_cb_ = connc;
    }

    /// Optional callback called on each health tick (every 10s).
    using IndicatorCallback = std::function<void()>;
    void set_indicator_callback(IndicatorCallback cb) { indicator_cb_ = cb; }

    /// Custom message posted to tray window when an update is detected.
    static constexpr UINT WM_UPDATE_AVAILABLE = WM_USER + 2;

    /// Force immediate tray tooltip refresh.
    static constexpr UINT WM_REFRESH_TRAY = WM_USER + 3;

    /// Update tray icon badge color (wParam = ControllerType, lParam = COLORREF).
    static constexpr UINT WM_TRAY_BADGE_UPDATE = WM_USER + 4;

  private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void handleTrayMessage(LPARAM lParam);
    void showContextMenu();
    void updateBadge(wininspect::ControllerType who, COLORREF color);
    void refreshTooltip();

    HWND hwnd_ = nullptr;
    HINSTANCE hInst_ = nullptr;
    OnExitCallback onExit_;
    bool running_ = false;

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    static constexpr UINT ID_TRAY_EXIT = 1001;
    static constexpr UINT ID_TRAY_ABOUT = 1002;
    static constexpr UINT ID_TRAY_UPDATE = 1003;

    UpdateState update_state_;
    std::shared_ptr<std::atomic<bool>> health_flag_;
    ControllerCallback controller_cb_;
    ConnectionCallback conn_cb_;
    IndicatorCallback indicator_cb_;
    int last_conn_count_ = 0;
    std::string last_controller_ = "none";
    static constexpr UINT_PTR HEALTH_TIMER_ID = 100;
  };

} // namespace wininspectd
#endif
