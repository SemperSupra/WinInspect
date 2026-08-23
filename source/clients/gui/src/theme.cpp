// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// Theme engine implementation.

#include "theme.hpp"

ThemeManager::ThemeManager()
{
  refresh_from_system();
  create_brushes();
}

void ThemeManager::set_mode(ThemeMode mode)
{
  mode_ = mode;
  if (mode == ThemeMode::Light)
    current_ = light_;
  else if (mode == ThemeMode::Dark)
    current_ = dark_;
  else
    refresh_from_system(); // System → read registry
  create_brushes();
}

void ThemeManager::refresh_from_system()
{
  if (mode_ == ThemeMode::System) {
    if (system_uses_light_theme())
      current_ = light_;
    else
      current_ = dark_;
  }
  // If not System mode, current_ is already correct from set_mode()
}

bool ThemeManager::system_uses_light_theme()
{
  HKEY hKey;
  LSTATUS status = RegOpenKeyExW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      0, KEY_READ, &hKey);

  if (status != ERROR_SUCCESS)
    return true; // Wine or locked down → fallback to light

  DWORD value = 1;
  DWORD size = sizeof(value);
  status = RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                            (LPBYTE)&value, &size);
  RegCloseKey(hKey);

  if (status != ERROR_SUCCESS)
    return true; // Key not found → fallback to light

  return value != 0; // 1 = light, 0 = dark
}

void ThemeManager::create_brushes()
{
  destroy_brushes();
  bg_brush_ = CreateSolidBrush(current_.background);
  surface_brush_ = CreateSolidBrush(current_.surface);
  highlight_brush_ = CreateSolidBrush(current_.highlight);
  border_pen_ = CreatePen(PS_SOLID, 1, current_.border);
}

void ThemeManager::destroy_brushes()
{
  if (bg_brush_) { DeleteObject(bg_brush_); bg_brush_ = nullptr; }
  if (surface_brush_) { DeleteObject(surface_brush_); surface_brush_ = nullptr; }
  if (highlight_brush_) { DeleteObject(highlight_brush_); highlight_brush_ = nullptr; }
  if (border_pen_) { DeleteObject(border_pen_); border_pen_ = nullptr; }
}
