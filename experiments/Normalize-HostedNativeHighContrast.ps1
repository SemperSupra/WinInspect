$ErrorActionPreference = 'Stop'
$themeHeaderPath = 'source/clients/gui/src/theme.hpp'
$themeSourcePath = 'source/clients/gui/src/theme.cpp'
$guiPath = 'source/clients/gui/src/gui_main.cpp'
$header = [IO.File]::ReadAllText($themeHeaderPath).Replace("`r`n", "`n")
$source = [IO.File]::ReadAllText($themeSourcePath).Replace("`r`n", "`n")
$gui = [IO.File]::ReadAllText($guiPath).Replace("`r`n", "`n")

function Replace-Exactly {
    param([ref]$Text,[string]$Old,[string]$New,[string]$Label)
    $first = $Text.Value.IndexOf($Old,[StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Expected $Label block not found." }
    if ($Text.Value.IndexOf($Old,$first + $Old.Length,[StringComparison]::Ordinal) -ge 0) {
        throw "Expected exactly one $Label block."
    }
    $Text.Value = $Text.Value.Substring(0,$first) + $New + $Text.Value.Substring($first + $Old.Length)
}

# High Contrast is an operating-system accessibility mode, not an application theme.
# It therefore overrides Light/Dark/System while active and derives all semantic colors
# from the user's Windows system-color palette.
$old = @'
inline ThemeColors make_dark_theme()
{
'@
$new = @'
inline ThemeColors make_high_contrast_theme()
{
  const COLORREF window = GetSysColor(COLOR_WINDOW);
  const COLORREF text = GetSysColor(COLOR_WINDOWTEXT);
  const COLORREF button = GetSysColor(COLOR_BTNFACE);
  const COLORREF highlight = GetSysColor(COLOR_HIGHLIGHT);
  return {
    window,     // background
    window,     // surface
    button,     // surfaceAlt
    text,       // border
    text,       // text
    text,       // textDim -- do not dim semantic text in High Contrast
    highlight,  // accent
    highlight,  // accentHover
    highlight,  // highlight
    text,       // success -- state remains explicit in text, not color alone
    text,       // error
    text,       // warning
  };
}

inline ThemeColors make_dark_theme()
{
'@
Replace-Exactly ([ref]$header) $old $new 'High Contrast palette insertion'

$old = @'
  /// Theme change has been detected (WM_SETTINGCHANGE)
  void on_system_theme_changed() { refresh_from_system(); }
'@
$new = @'
  /// Theme/accessibility color change has been detected.
  void on_system_theme_changed() { refresh_from_system(); }

  /// True when Windows High Contrast is currently active.
  bool high_contrast() const { return high_contrast_; }
'@
Replace-Exactly ([ref]$header) $old $new 'public High Contrast state'

$old = @'
  ThemeColors current_ = light_;

  HBRUSH bg_brush_ = nullptr;
'@
$new = @'
  ThemeColors current_ = light_;
  bool high_contrast_ = false;

  HBRUSH bg_brush_ = nullptr;
'@
Replace-Exactly ([ref]$header) $old $new 'High Contrast state storage'

$old = @'
  static bool system_uses_light_theme();
};
'@
$new = @'
  static bool system_uses_light_theme();
  static bool system_high_contrast();
};
'@
Replace-Exactly ([ref]$header) $old $new 'High Contrast system query declaration'

$old = @'
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
'@
$new = @'
void ThemeManager::set_mode(ThemeMode mode)
{
  mode_ = mode;
  refresh_from_system();
  create_brushes();
}

void ThemeManager::refresh_from_system()
{
  high_contrast_ = system_high_contrast();
  if (high_contrast_) {
    current_ = make_high_contrast_theme();
    return;
  }

  if (mode_ == ThemeMode::Light)
    current_ = light_;
  else if (mode_ == ThemeMode::Dark)
    current_ = dark_;
  else
    current_ = system_uses_light_theme() ? light_ : dark_;
}

bool ThemeManager::system_high_contrast()
{
  HIGHCONTRASTW hc = {};
  hc.cbSize = sizeof(hc);
  if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0))
    return false;
  return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}
'@
Replace-Exactly ([ref]$source) $old $new 'High Contrast detection and theme precedence'

$old = @'
      case WM_DPICHANGED:  self->onDpiChanged(wParam, lParam); return 0;
      case WM_SETTINGCHANGE:
        if (lParam) { std::wstring a((LPCWSTR)lParam); if (a == L"ImmersiveColorSet") self->onThemeChanged(); }
        return 0;
'@
$new = @'
      case WM_DPICHANGED:  self->onDpiChanged(wParam, lParam); return 0;
      case WM_SYSCOLORCHANGE:
        self->onThemeChanged();
        return 0;
      case WM_SETTINGCHANGE:
        if (wParam == SPI_SETHIGHCONTRAST) {
          self->onThemeChanged();
        } else if (lParam) {
          std::wstring a((LPCWSTR)lParam);
          if (a == L"ImmersiveColorSet") self->onThemeChanged();
        }
        return 0;
'@
Replace-Exactly ([ref]$gui) $old $new 'High Contrast system-change handling'

$old = @'
    // Capture preview: darker background
    if (hc == hCapturePreview_) {
      SetBkColor(hdc, RGB(0x11, 0x13, 0x18));
      SetTextColor(hdc, RGB(0x55, 0x55, 0x55));
      return (LRESULT)GetStockObject(BLACK_BRUSH);
    }
'@
$new = @'
    // Preserve the preview treatment normally, but never override the user's
    // High Contrast foreground/background choices.
    if (hc == hCapturePreview_) {
      if (theme_.high_contrast()) {
        SetBkColor(hdc, c.background);
        SetTextColor(hdc, c.text);
        return (LRESULT)theme_.background_brush();
      }
      SetBkColor(hdc, RGB(0x11, 0x13, 0x18));
      SetTextColor(hdc, RGB(0x55, 0x55, 0x55));
      return (LRESULT)GetStockObject(BLACK_BRUSH);
    }
'@
Replace-Exactly ([ref]$gui) $old $new 'High Contrast capture preview'

$old = @'
    if (hc == hStatus_) {
      SetTextColor(hdc, transport_->connected() ? RGB(0x2e, 0x7d, 0x32) : RGB(0xc6, 0x28, 0x28));
      SetBkColor(hdc, c.background);
      return (LRESULT)theme_.background_brush();
    }
'@
$new = @'
    if (hc == hStatus_) {
      SetTextColor(hdc, transport_->connected() ? c.success : c.error);
      SetBkColor(hdc, c.background);
      return (LRESULT)theme_.background_brush();
    }
'@
Replace-Exactly ([ref]$gui) $old $new 'theme-owned status colors'

$old = '  void onThemeChanged() { theme_.on_system_theme_changed(); applyTheme(); }'
$new = '  void onThemeChanged() { theme_.on_system_theme_changed(); applyTheme(); pollDaemonStatus(); }'
Replace-Exactly ([ref]$gui) $old $new 'theme-change daemon recolor'

$gui = $gui.Replace('  COLORREF daemon_indicator_color_ = RGB(0x2e, 0x7d, 0x32); // green default',
                    '  COLORREF daemon_indicator_color_ = GetSysColor(COLOR_WINDOWTEXT);')
$gui = $gui.Replace('daemon_indicator_color_ = RGB(0xc6, 0x28, 0x28); // red', 'daemon_indicator_color_ = theme_.colors().error;')
$gui = $gui.Replace('daemon_indicator_color_ = RGB(0x2e, 0x7d, 0x32);', 'daemon_indicator_color_ = theme_.colors().success;')
$gui = $gui.Replace('daemon_indicator_color_ = RGB(0x21, 0x96, 0xf3);', 'daemon_indicator_color_ = theme_.colors().accent;')
$gui = $gui.Replace('daemon_indicator_color_ = RGB(0xff, 0x98, 0x00);', 'daemon_indicator_color_ = theme_.colors().warning;')

foreach ($needle in @('daemon_indicator_color_ = RGB(0xc6, 0x28, 0x28)',
                       'daemon_indicator_color_ = RGB(0x2e, 0x7d, 0x32)',
                       'daemon_indicator_color_ = RGB(0x21, 0x96, 0xf3)',
                       'daemon_indicator_color_ = RGB(0xff, 0x98, 0x00)',
                       'transport_->connected() ? RGB(0x2e, 0x7d, 0x32)')) {
    if ($gui.Contains($needle)) { throw "High Contrast normalization left hard-coded semantic color: $needle" }
}

[IO.File]::WriteAllText($themeHeaderPath,$header,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($themeSourcePath,$source,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($guiPath,$gui,[Text.UTF8Encoding]::new($false))
Write-Host 'Windows High Contrast candidate installed: SPI_GETHIGHCONTRAST, GetSysColor palette, system-change refresh, and semantic colors normalized.'
