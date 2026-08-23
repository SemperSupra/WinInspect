#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// Theme engine for WinInspect GUI — Light, Dark, and System modes.
// Compatible with Windows 10/11 and Wine 10/11/12.
// - System mode reads AppsUseLightTheme from registry
// - Wine fallback: Light theme (registry key may not exist)
// - Dynamic switching on WM_SETTINGCHANGE

#include <windows.h>
#include <string>

// ── Theme identifiers for menu and persistence ──────────────────────────

enum class ThemeMode : int {
  Light = 0,
  Dark = 1,
  System = 2,
};

// ── Color palette ──────────────────────────────────────────────────────

struct ThemeColors {
  COLORREF background;   // Main window background
  COLORREF surface;      // Card/panel surface, control backgrounds
  COLORREF surfaceAlt;   // Alternate surface (hover, selection)
  COLORREF border;       // Control borders
  COLORREF text;         // Primary text color
  COLORREF textDim;      // Secondary/label/text
  COLORREF accent;       // Buttons, highlights, links
  COLORREF accentHover;  // Button hover state
  COLORREF highlight;    // Selection highlight (tree/list selection)
  COLORREF success;      // Status OK green
  COLORREF error;        // Status error red
  COLORREF warning;      // Status warning amber
};

// ── Predefined palettes ────────────────────────────────────────────────

inline ThemeColors make_light_theme()
{
  return {
    RGB(0xf0, 0xf2, 0xf5),  // background
    RGB(0xff, 0xff, 0xff),  // surface
    RGB(0xf5, 0xf6, 0xf8),  // surfaceAlt
    RGB(0xe0, 0xe3, 0xe8),  // border
    RGB(0x1a, 0x1a, 0x2e),  // text
    RGB(0x88, 0x88, 0x88),  // textDim
    RGB(0x15, 0x65, 0xc0),  // accent
    RGB(0x19, 0x76, 0xd2),  // accentHover
    RGB(0xe3, 0xf2, 0xfd),  // highlight
    RGB(0x2e, 0x7d, 0x32),  // success
    RGB(0xc6, 0x28, 0x28),  // error
    RGB(0xe6, 0x51, 0x00),  // warning
  };
}

inline ThemeColors make_dark_theme()
{
  return {
    RGB(0x1a, 0x1d, 0x23),  // background
    RGB(0x1e, 0x21, 0x28),  // surface
    RGB(0x23, 0x27, 0x30),  // surfaceAlt
    RGB(0x2a, 0x2d, 0x35),  // border
    RGB(0xe0, 0xe0, 0xe0),  // text
    RGB(0x88, 0x88, 0x88),  // textDim
    RGB(0x15, 0x65, 0xc0),  // accent
    RGB(0x19, 0x76, 0xd2),  // accentHover
    RGB(0x1e, 0x24, 0x30),  // highlight
    RGB(0x4c, 0xaf, 0x50),  // success
    RGB(0xe5, 0x39, 0x35),  // error
    RGB(0xfd, 0xd8, 0x35),  // warning
  };
}

// ── Theme Manager ──────────────────────────────────────────────────────

class ThemeManager
{
public:
  ThemeManager();

  /// Get current effective colors (resolves System mode)
  const ThemeColors& colors() const { return current_; }

  /// Get/set mode
  ThemeMode mode() const { return mode_; }
  void set_mode(ThemeMode mode);

  /// Re-read registry and update if in System mode
  void refresh_from_system();

  /// Theme change has been detected (WM_SETTINGCHANGE)
  void on_system_theme_changed() { refresh_from_system(); }

  /// Create brushes for current theme (call after theme changes)
  void create_brushes();
  void destroy_brushes();

  /// GDI objects for current theme
  HBRUSH background_brush() const { return bg_brush_; }
  HBRUSH surface_brush() const { return surface_brush_; }
  HBRUSH highlight_brush() const { return highlight_brush_; }
  HPEN border_pen() const { return border_pen_; }

private:
  ThemeMode mode_ = ThemeMode::System;
  ThemeColors light_ = make_light_theme();
  ThemeColors dark_ = make_dark_theme();
  ThemeColors current_ = light_;

  HBRUSH bg_brush_ = nullptr;
  HBRUSH surface_brush_ = nullptr;
  HBRUSH highlight_brush_ = nullptr;
  HPEN border_pen_ = nullptr;

  /// Read Windows theme setting from registry.
  /// Returns true if system uses light theme, false if dark.
  /// On Wine or error, returns true (fallback to light).
  static bool system_uses_light_theme();
};
