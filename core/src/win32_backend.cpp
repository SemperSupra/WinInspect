#include "wininspect/win32_backend.hpp"
#include "wininspect/util_win32.hpp"
#include <chrono>
#include <thread>
#include <regex>
#include <tlhelp32.h>
#include <winsvc.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <uiautomation.h>
#include <comdef.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <windows.h>

namespace wininspect {
// ... existing helper functions remain same ...

static hwnd_u64 to_u64(HWND h) {
  return static_cast<hwnd_u64>(reinterpret_cast<std::uintptr_t>(h));
}
static HWND from_u64(hwnd_u64 h) {
  return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(h));
}

static std::string w2u8(const std::wstring &ws) {
  if (ws.empty())
    return {};
  int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr,
                                0, nullptr, nullptr);
  std::string out(len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), len,
                      nullptr, nullptr);
  return out;
}

static std::string bstr_to_utf8(BSTR bstr) {
  if (!bstr)
    return {};
  std::wstring ws(bstr, SysStringLen(bstr));
  return w2u8(ws);
}

static std::wstring get_window_text_w(HWND hwnd) {
  int n = GetWindowTextLengthW(hwnd);
  std::wstring w;
  w.resize((size_t)n + 1);
  GetWindowTextW(hwnd, w.data(), n + 1);
  w.resize((size_t)n);
  return w;
}

static std::wstring get_class_name_w(HWND hwnd) {
  wchar_t buf[256];
  int n = GetClassNameW(hwnd, buf, 256);
  return std::wstring(buf, buf + (n > 0 ? n : 0));
}

static std::string try_process_image_path(DWORD pid) {
  std::string out;
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h)
    return out;
  wchar_t buf[32768];
  DWORD sz = (DWORD)(sizeof(buf) / sizeof(buf[0]));
  if (QueryFullProcessImageNameW(h, 0, buf, &sz)) {
    out = w2u8(std::wstring(buf, buf + sz));
  }
  CloseHandle(h);
  return out;
}

Snapshot Win32Backend::capture_snapshot() {
  Snapshot s;
  EnumWindows(
      [](HWND h, LPARAM lp) -> BOOL {
        auto *vec = reinterpret_cast<std::vector<hwnd_u64> *>(lp);
        vec->push_back(to_u64(h));
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&s.top));
  return s;
}

std::vector<hwnd_u64> Win32Backend::list_top(const Snapshot &s) {
  return s.top;
}

std::vector<hwnd_u64> Win32Backend::list_children(const Snapshot &,
                                                  hwnd_u64 parent) {
  std::vector<hwnd_u64> out;
  EnumChildWindows(
      from_u64(parent),
      [](HWND h, LPARAM lp) -> BOOL {
        auto *vec = reinterpret_cast<std::vector<hwnd_u64> *>(lp);
        vec->push_back(to_u64(h));
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&out));
  return out;
}

static std::vector<std::string> parse_ws(std::uint64_t style) {
  static const std::vector<std::pair<std::uint64_t, std::string>> flags = {
      {WS_OVERLAPPED, "WS_OVERLAPPED"},
      {WS_POPUP, "WS_POPUP"},
      {WS_CHILD, "WS_CHILD"},
      {WS_MINIMIZE, "WS_MINIMIZE"},
      {WS_VISIBLE, "WS_VISIBLE"},
      {WS_DISABLED, "WS_DISABLED"},
      {WS_CLIPSIBLINGS, "WS_CLIPSIBLINGS"},
      {WS_CLIPCHILDREN, "WS_CLIPCHILDREN"},
      {WS_MAXIMIZE, "WS_MAXIMIZE"},
      {WS_CAPTION, "WS_CAPTION"},
      {WS_BORDER, "WS_BORDER"},
      {WS_DLGFRAME, "WS_DLGFRAME"},
      {WS_VSCROLL, "WS_VSCROLL"},
      {WS_HSCROLL, "WS_VSCROLL"},
      {WS_SYSMENU, "WS_SYSMENU"},
      {WS_THICKFRAME, "WS_THICKFRAME"},
      {WS_GROUP, "WS_GROUP"},
      {WS_TABSTOP, "WS_TABSTOP"},
      {WS_MINIMIZEBOX, "WS_MINIMIZEBOX"},
      {WS_MAXIMIZEBOX, "WS_MAXIMIZEBOX"}};
  std::vector<std::string> out;
  for (const auto &f : flags) {
    if ((style & f.first) == f.first && f.first != 0)
      out.push_back(f.second);
  }
  if (style == 0)
    out.push_back("WS_OVERLAPPED");
  return out;
}

static std::vector<std::string> parse_ws_ex(std::uint64_t exstyle) {
  static const std::vector<std::pair<std::uint64_t, std::string>> flags = {
      {WS_EX_DLGMODALFRAME, "WS_EX_DLGMODALFRAME"},
      {WS_EX_NOPARENTNOTIFY, "WS_EX_NOPARENTNOTIFY"},
      {WS_EX_TOPMOST, "WS_EX_TOPMOST"},
      {WS_EX_ACCEPTFILES, "WS_EX_ACCEPTFILES"},
      {WS_EX_TRANSPARENT, "WS_EX_TRANSPARENT"},
      {WS_EX_MDICHILD, "WS_EX_MDICHILD"},
      {WS_EX_TOOLWINDOW, "WS_EX_TOOLWINDOW"},
      {WS_EX_WINDOWEDGE, "WS_EX_WINDOWEDGE"},
      {WS_EX_CLIENTEDGE, "WS_EX_CLIENTEDGE"},
      {WS_EX_CONTEXTHELP, "WS_EX_CONTEXTHELP"},
      {WS_EX_RIGHT, "WS_EX_RIGHT"},
      {WS_EX_LEFT, "WS_EX_LEFT"},
      {WS_EX_RTLREADING, "WS_EX_RTLREADING"},
      {WS_EX_LTRREADING, "WS_EX_LTRREADING"},
      {WS_EX_LEFTSCROLLBAR, "WS_EX_LEFTSCROLLBAR"},
      {WS_EX_RIGHTSCROLLBAR, "WS_EX_RIGHTSCROLLBAR"},
      {WS_EX_CONTROLPARENT, "WS_EX_CONTROLPARENT"},
      {WS_EX_STATICEDGE, "WS_EX_STATICEDGE"},
      {WS_EX_APPWINDOW, "WS_EX_APPWINDOW"},
      {WS_EX_LAYERED, "WS_EX_LAYERED"},
      {WS_EX_NOINHERITLAYOUT, "WS_EX_NOINHERITLAYOUT"},
      {WS_EX_LAYOUTRTL, "WS_EX_LAYOUTRTL"},
      {WS_EX_COMPOSITED, "WS_EX_COMPOSITED"},
      {WS_EX_NOACTIVATE, "WS_EX_NOACTIVATE"}};
  std::vector<std::string> out;
  for (const auto &f : flags) {
    if ((exstyle & f.first) == f.first)
      out.push_back(f.second);
  }
  return out;
}

std::optional<WindowInfo> Win32Backend::get_info(const Snapshot &,
                                                 hwnd_u64 hwnd_u) {
  HWND hwnd = from_u64(hwnd_u);
  if (!IsWindow(hwnd))
    return std::nullopt;

  WindowInfo wi{};
  wi.hwnd = hwnd_u;
  wi.parent = to_u64(GetParent(hwnd));
  wi.owner = to_u64(GetWindow(hwnd, GW_OWNER));
  wi.class_name = w2u8(get_class_name_w(hwnd));
  wi.title = w2u8(get_window_text_w(hwnd));

  RECT r{};
  GetWindowRect(hwnd, &r);
  wi.window_rect = {r.left, r.top, r.right, r.bottom};

  RECT cr{};
  GetClientRect(hwnd, &cr);
  wi.client_rect = {cr.left, cr.top, cr.right, cr.bottom};

  POINT pt = {0, 0};
  ClientToScreen(hwnd, &pt);
  wi.screen_rect = {pt.x, pt.y, pt.x + (cr.right - cr.left),
                    pt.y + (cr.bottom - cr.top)};

  DWORD pid = 0;
  wi.tid = GetWindowThreadProcessId(hwnd, &pid);
  wi.pid = pid;

  LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  LONG_PTR exsty = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  wi.style = (std::uint64_t)(std::uintptr_t)style;
  wi.exstyle = (std::uint64_t)(std::uintptr_t)exsty;

  wi.style_flags = parse_ws(wi.style);
  wi.ex_style_flags = parse_ws_ex(wi.exstyle);

  wi.visible = IsWindowVisible(hwnd) != FALSE;
  wi.enabled = IsWindowEnabled(hwnd) != FALSE;
  wi.iconic = IsIconic(hwnd) != FALSE;
  wi.zoomed = IsZoomed(hwnd) != FALSE;

  wi.process_image = try_process_image_path(pid);
  return wi;
}

std::optional<hwnd_u64> Win32Backend::pick_at_point(const Snapshot &, int x,
                                                    int y, PickFlags flags) {
  POINT pt{x, y};
  HWND h = WindowFromPoint(pt);
  if (!h)
    return std::nullopt;

  if (flags.prefer_child) {
    HWND child = ChildWindowFromPointEx(
        h, pt, flags.ignore_transparent ? CWP_SKIPTRANSPARENT : 0);
    if (child)
      h = child;
  }
  return to_u64(h);
}

std::vector<WindowNode> Win32Backend::get_window_tree(const Snapshot &,
                                                   hwnd_u64 root_u) {
  std::vector<WindowNode> results;
  HWND root = (root_u == 0) ? GetDesktopWindow() : from_u64(root_u);

  struct Param {
    std::vector<WindowNode> *nodes;
    Win32Backend *backend;
  };
  Param p{&results, this};

  EnumChildWindows(
      root,
      [](HWND h, LPARAM lp) -> BOOL {
        auto *param = reinterpret_cast<Param *>(lp);
        // Only immediate children for the first level of recursion
        if (GetParent(h) == from_u64(to_u64(GetParent(h)))) { // placeholder logic
          // Real recursive tree would be better, but EnumChildWindows is flat.
          // Let's implement a proper recursive builder.
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&p));

  // Correct recursive implementation:
  auto build_node = [&](auto self, HWND h) -> WindowNode {
    WindowNode node;
    node.hwnd = to_u64(h);
    node.title = w2u8(get_window_text_w(h));
    node.class_name = w2u8(get_class_name_w(h));

    struct ChildParam {
      std::vector<WindowNode> *children;
      decltype(self) *recurse;
    };
    ChildParam cp{&node.children, &self};

    // EnumChildWindows is recursive by default, we need GetWindow loop for
    // immediate children
    HWND child = GetWindow(h, GW_CHILD);
    while (child) {
      node.children.push_back(self(self, child));
      child = GetWindow(child, GW_HWNDNEXT);
    }
    return node;
  };

  if (root_u == 0) {
    // For desktop, list top-level windows
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
          auto *vec = reinterpret_cast<std::vector<WindowNode> *>(lp);
          // We don't recurse everything for desktop to avoid huge trees
          WindowNode n;
          n.hwnd = to_u64(h);
          n.title = w2u8(get_window_text_w(h));
          n.class_name = w2u8(get_class_name_w(h));
          vec->push_back(n);
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&results));
  } else {
    results.push_back(build_node(build_node, root));
  }

  return results;
}

static std::string base64_encode(const std::vector<uint8_t> &in) {
  static const char b64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, valb = -6;
  for (uint8_t c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(b64[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6)
    out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4)
    out.push_back('=');
  return out;
}

std::string Color::to_hex() const {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
  return buf;
}

bool Win32Backend::set_property(hwnd_u64 hwnd_u, const std::string &name,
                                const std::string &value) {
  HWND hwnd = from_u64(hwnd_u);
  if (!IsWindow(hwnd))
    return false;

  if (name == "topmost") {
    bool top = (value == "true");
    return SetWindowPos(hwnd, top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE) != FALSE;
  } else if (name == "opacity") {
    int alpha = std::stoi(value);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    
    LONG_PTR exstyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exstyle | WS_EX_LAYERED);
    return SetLayeredWindowAttributes(hwnd, 0, (BYTE)alpha, LWA_ALPHA) != FALSE;
  }
  return false;
}

bool Win32Backend::highlight_window(hwnd_u64 hwnd_u) {
  HWND hwnd = from_u64(hwnd_u);
  if (!IsWindow(hwnd))
    return false;

  RECT r;
  GetWindowRect(hwnd, &r);
  HDC hdc = GetDC(NULL);
  if (!hdc) return false;

  HPEN pen = CreatePen(PS_SOLID, 5, RGB(255, 0, 0));
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

  for (int i = 0; i < 3; ++i) {
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    InvalidateRect(NULL, &r, TRUE);
    UpdateWindow(NULL);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  SelectObject(hdc, oldPen);
  SelectObject(hdc, oldBrush);
  DeleteObject(pen);
  ReleaseDC(NULL, hdc);
  return true;
}

bool Win32Backend::control_click(hwnd_u64 hwnd_u, int x, int y, int button) {
  HWND hwnd = from_u64(hwnd_u);
  if (!IsWindow(hwnd))
    return false;

  LPARAM lp = MAKELPARAM(x, y);
  uint32_t down = WM_LBUTTONDOWN;
  uint32_t up = WM_LBUTTONUP;

  if (button == 1) { down = WM_RBUTTONDOWN; up = WM_RBUTTONUP; }
  else if (button == 2) { down = WM_MBUTTONDOWN; up = WM_MBUTTONUP; }

  PostMessageW(hwnd, down, MK_LBUTTON, lp);
  PostMessageW(hwnd, up, 0, lp);
  return true;
}

bool Win32Backend::control_send(hwnd_u64 hwnd_u, const std::string &text) {
  HWND hwnd = from_u64(hwnd_u);
  if (!IsWindow(hwnd))
    return false;

  std::wstring wtext;
  int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
  if (len > 0) {
    wtext.resize(len - 1);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), len);
  }

  for (wchar_t c : wtext) {
    PostMessageW(hwnd, WM_CHAR, c, 0);
  }
  return true;
}

std::optional<Color> Win32Backend::get_pixel(int x, int y) {
  HDC hdc = GetDC(NULL);
  if (!hdc) return std::nullopt;
  COLORREF c = GetPixel(hdc, x, y);
  ReleaseDC(NULL, hdc);
  if (c == CLR_INVALID) return std::nullopt;
  return Color{GetRValue(c), GetGValue(c), GetBValue(c)};
}

std::optional<std::pair<int, int>> Win32Backend::pixel_search(Rect region, Color target, int variation) {
  HDC hdc = GetDC(NULL);
  if (!hdc) return std::nullopt;

  for (int y = region.top; y < region.bottom; ++y) {
    for (int x = region.left; x < region.right; ++x) {
      COLORREF c = GetPixel(hdc, x, y);
      if (c == CLR_INVALID) continue;
      
      int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
      if (std::abs(r - target.r) <= variation &&
          std::abs(g - target.g) <= variation &&
          std::abs(b - target.b) <= variation) {
        ReleaseDC(NULL, hdc);
        return std::make_pair(x, y);
      }
    }
  }
  ReleaseDC(NULL, hdc);
  return std::nullopt;
}

std::vector<ProcessInfo> Win32Backend::list_processes() {
  std::vector<ProcessInfo> out;
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap == INVALID_HANDLE_VALUE) return out;

  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);
  if (Process32FirstW(hSnap, &pe)) {
    do {
      ProcessInfo pi;
      pi.pid = pe.th32ProcessID;
      pi.name = w2u8(pe.szExeFile);
      pi.path = try_process_image_path(pe.th32ProcessID);
      out.push_back(pi);
    } while (Process32NextW(hSnap, &pe));
  }
  CloseHandle(hSnap);
  return out;
}

bool Win32Backend::kill_process(uint32_t pid) {
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
  if (!h) return false;
  bool ok = TerminateProcess(h, 1) != FALSE;
  CloseHandle(h);
  return ok;
}

std::optional<ScreenCapture> Win32Backend::capture_screen(Rect region) {
  int w = region.right - region.left;
  int h = region.bottom - region.top;
  if (w <= 0 || h <= 0) return std::nullopt;

  HDC hdcScreen = GetDC(NULL);
  HDC hdcMem = CreateCompatibleDC(hdcScreen);
  HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
  SelectObject(hdcMem, hbm);

  BitBlt(hdcMem, 0, 0, w, h, hdcScreen, region.left, region.top, SRCCOPY);

  BITMAP bmp;
  GetObject(hbm, sizeof(BITMAP), &bmp);

  BITMAPFILEHEADER bfh = {0};
  bfh.bfType = 0x4D42;
  bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  bfh.bfSize = bfh.bfOffBits + bmp.bmWidthBytes * bmp.bmHeight;

  BITMAPINFOHEADER bih = {0};
  bih.biSize = sizeof(BITMAPINFOHEADER);
  bih.biWidth = bmp.bmWidth;
  bih.biHeight = bmp.bmHeight;
  bih.biPlanes = 1;
  bih.biBitCount = 24;
  bih.biCompression = BI_RGB;

  std::vector<uint8_t> buffer;
  buffer.resize(bfh.bfSize);
  memcpy(buffer.data(), &bfh, sizeof(bfh));
  memcpy(buffer.data() + sizeof(bfh), &bih, sizeof(bih));
  GetDIBits(hdcMem, hbm, 0, h, buffer.data() + bfh.bfOffBits, (BITMAPINFO*)&bih, DIB_RGB_COLORS);

  ScreenCapture sc;
  sc.width = w;
  sc.height = h;
  sc.data_b64 = base64_encode(buffer);

  DeleteObject(hbm);
  DeleteDC(hdcMem);
  ReleaseDC(NULL, hdcScreen);
  return sc;
}

std::optional<FileInfo> Win32Backend::get_file_info(const std::string &path) {
  std::wstring wpath(path.begin(), path.end());
  WIN32_FILE_ATTRIBUTE_DATA attr;
  if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr))
    return std::nullopt;

  FileInfo fi;
  fi.path = path;
  fi.size = ((uint64_t)attr.nFileSizeHigh << 32) | attr.nFileSizeLow;
  fi.is_directory = (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  fi.last_modified = "TODO"; // Simplified
  return fi;
}

std::optional<std::string> Win32Backend::read_file_content(const std::string &path) {
  std::wstring wpath(path.begin(), path.end());
  HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return std::nullopt;

  DWORD size = GetFileSize(h, NULL);
  std::string out;
  out.resize(size);
  DWORD read;
  ReadFile(h, out.data(), size, &read, NULL);
  CloseHandle(h);
  return out;
}

static HKEY parse_hkey(const std::string &path, std::string &subpath) {
  size_t first_slash = path.find('\\');
  std::string root = (first_slash == std::string::npos) ? path : path.substr(0, first_slash);
  subpath = (first_slash == std::string::npos) ? "" : path.substr(first_slash + 1);

  if (root == "HKEY_LOCAL_MACHINE" || root == "HKLM") return HKEY_LOCAL_MACHINE;
  if (root == "HKEY_CURRENT_USER" || root == "HKCU") return HKEY_CURRENT_USER;
  if (root == "HKEY_CLASSES_ROOT" || root == "HKCR") return HKEY_CLASSES_ROOT;
  if (root == "HKEY_USERS" || root == "HKU") return HKEY_USERS;
  return NULL;
}

std::vector<hwnd_u64> Win32Backend::find_windows_regex(const std::string &title_re,
                                                       const std::string &class_re) {
  std::vector<hwnd_u64> out;
  std::regex re_t(title_re), re_c(class_re);

  struct RegexParam {
    std::vector<hwnd_u64> *out;
    std::regex *re_t;
    std::regex *re_c;
  } p = { &out, &re_t, &re_c };

  EnumWindows([](HWND h, LPARAM lp) -> BOOL {
    auto* p = reinterpret_cast<RegexParam*>(lp);
    std::string title = w2u8(get_window_text_w(h));
    std::string cls = w2u8(get_class_name_w(h));
    
    if (std::regex_search(title, *(p->re_t)) && std::regex_search(cls, *(p->re_c))) {
      p->out->push_back(to_u64(h));
    }
    return TRUE;
  }, reinterpret_cast<LPARAM>(&p));
  
  return out;
}

std::optional<RegistryKeyInfo> Win32Backend::reg_read(const std::string &path) {
  std::string subpath;
  HKEY root = parse_hkey(path, subpath);
  if (!root) return std::nullopt;

  HKEY hKey;
  std::wstring wsubpath(subpath.begin(), subpath.end());
  if (RegOpenKeyExW(root, wsubpath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return std::nullopt;

  RegistryKeyInfo info;
  info.path = path;

  // Enumerate Subkeys
  wchar_t name[256];
  DWORD name_size = 256;
  for (DWORD i = 0; RegEnumKeyExW(hKey, i, name, &name_size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS; ++i) {
    info.subkeys.push_back(w2u8(name));
    name_size = 256;
  }

  // Enumerate Values
  DWORD val_name_size = 256;
  DWORD type;
  BYTE data[4096];
  DWORD data_size = 4096;
  for (DWORD i = 0; RegEnumValueW(hKey, i, name, &val_name_size, NULL, &type, data, &data_size) == ERROR_SUCCESS; ++i) {
    RegistryValue rv;
    rv.name = w2u8(name);
    if (type == REG_SZ) {
      rv.type = "SZ";
      rv.data = w2u8((wchar_t*)data);
    } else if (type == REG_DWORD) {
      rv.type = "DWORD";
      rv.data = std::to_string(*(DWORD*)data);
    } else {
      rv.type = "BINARY";
      rv.data = "(binary data)";
    }
    info.values.push_back(rv);
    val_name_size = 256;
    data_size = 4096;
  }

  RegCloseKey(hKey);
  return info;
}

bool Win32Backend::reg_write(const std::string &path, const RegistryValue &val) {
  std::string subpath;
  HKEY root = parse_hkey(path, subpath);
  if (!root) return false;

  HKEY hKey;
  std::wstring wsubpath(subpath.begin(), subpath.end());
  if (RegCreateKeyExW(root, wsubpath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
    return false;

  std::wstring wname(val.name.begin(), val.name.end());
  LSTATUS status = ERROR_INVALID_PARAMETER;

  if (val.type == "SZ") {
    std::wstring wdata(val.data.begin(), val.data.end());
    status = RegSetValueExW(hKey, wname.c_str(), 0, REG_SZ, (BYTE*)wdata.c_str(), (DWORD)(wdata.size() + 1) * 2);
  } else if (val.type == "DWORD") {
    DWORD d = std::stoul(val.data);
    status = RegSetValueExW(hKey, wname.c_str(), 0, REG_DWORD, (BYTE*)&d, sizeof(DWORD));
  }

  RegCloseKey(hKey);
  return status == ERROR_SUCCESS;
}

bool Win32Backend::reg_delete(const std::string &path, const std::string &value_name) {
  std::string subpath;
  HKEY root = parse_hkey(path, subpath);
  if (!root) return false;

  std::wstring wsubpath(subpath.begin(), subpath.end());
  if (value_name.empty()) {
    return RegDeleteKeyW(root, wsubpath.c_str()) == ERROR_SUCCESS;
  } else {
    HKEY hKey;
    if (RegOpenKeyExW(root, wsubpath.c_str(), 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
      return false;
    std::wstring wname(value_name.begin(), value_name.end());
    bool ok = RegDeleteValueW(hKey, wname.c_str()) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return ok;
  }
}

bool Win32Backend::reg_subscribe(const std::string &) {
  return true; 
}

// Clipboard
std::optional<std::string> Win32Backend::clipboard_read() {
  if (!OpenClipboard(NULL)) return std::nullopt;
  std::string out;
  HANDLE hData = GetClipboardData(CF_UNICODETEXT);
  if (hData) {
    wchar_t *p = (wchar_t *)GlobalLock(hData);
    if (p) {
      out = w2u8(p);
      GlobalUnlock(hData);
    }
  }
  CloseClipboard();
  return hData ? std::optional<std::string>{out} : std::nullopt;
}

bool Win32Backend::clipboard_write(const std::string &text) {
  if (!OpenClipboard(NULL)) return false;
  EmptyClipboard();
  std::wstring wtext(text.begin(), text.end()); // Simple conversion
  int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
  if (len > 0) {
    wtext.resize(len);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), len);
  }
  
  HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wtext.size() + 1) * sizeof(wchar_t));
  if (hMem) {
    void *p = GlobalLock(hMem);
    memcpy(p, wtext.c_str(), (wtext.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
  }
  CloseClipboard();
  return hMem != NULL;
}

// Services
std::vector<ServiceInfo> Win32Backend::service_list() {
  std::vector<ServiceInfo> out;
  SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
  if (!hSCM) return out;

  DWORD bytesNeeded = 0, count = 0, resume = 0;
  EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                       NULL, 0, &bytesNeeded, &count, &resume, NULL);
  
  if (GetLastError() == ERROR_MORE_DATA) {
    std::vector<BYTE> buf(bytesNeeded);
    LPENUM_SERVICE_STATUS_PROCESSW pInfo = (LPENUM_SERVICE_STATUS_PROCESSW)buf.data();
    if (EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                             buf.data(), bytesNeeded, &bytesNeeded, &count, &resume, NULL)) {
      for (DWORD i = 0; i < count; ++i) {
        ServiceInfo si;
        si.name = w2u8(pInfo[i].lpServiceName);
        si.display_name = w2u8(pInfo[i].lpDisplayName);
        si.state = (pInfo[i].ServiceStatusProcess.dwCurrentState == SERVICE_RUNNING) ? "RUNNING" : "STOPPED";
        out.push_back(si);
      }
    }
  }
  CloseServiceHandle(hSCM);
  return out;
}

std::string Win32Backend::service_status(const std::string &name) {
  SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!hSCM) return "UNKNOWN";
  std::wstring wname(name.begin(), name.end());
  SC_HANDLE hSvc = OpenServiceW(hSCM, wname.c_str(), SERVICE_QUERY_STATUS);
  if (!hSvc) { CloseServiceHandle(hSCM); return "NOT_FOUND"; }
  
  SERVICE_STATUS_PROCESS ssp;
  DWORD bytes = 0;
  std::string status = "UNKNOWN";
  if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytes)) {
    switch (ssp.dwCurrentState) {
      case SERVICE_RUNNING: status = "RUNNING"; break;
      case SERVICE_STOPPED: status = "STOPPED"; break;
      case SERVICE_START_PENDING: status = "STARTING"; break;
      case SERVICE_STOP_PENDING: status = "STOPPING"; break;
      case SERVICE_PAUSED: status = "PAUSED"; break;
      default: status = "OTHER"; break;
    }
  }
  CloseServiceHandle(hSvc);
  CloseServiceHandle(hSCM);
  return status;
}

bool Win32Backend::service_control(const std::string &name, const std::string &action) {
  SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!hSCM) return false;
  std::wstring wname(name.begin(), name.end());
  DWORD access = (action == "start") ? SERVICE_START : (SERVICE_STOP | SERVICE_QUERY_STATUS);
  SC_HANDLE hSvc = OpenServiceW(hSCM, wname.c_str(), access);
  if (!hSvc) { CloseServiceHandle(hSCM); return false; }

  bool ok = false;
  if (action == "start") {
    ok = StartServiceW(hSvc, 0, NULL) != FALSE;
  } else if (action == "stop") {
    SERVICE_STATUS status;
    ok = ControlService(hSvc, SERVICE_CONTROL_STOP, &status) != FALSE;
  }
  CloseServiceHandle(hSvc);
  CloseServiceHandle(hSCM);
  return ok;
}

// Env
std::vector<EnvVar> Win32Backend::env_get_all() {
  std::vector<EnvVar> out;
  wchar_t *env = GetEnvironmentStringsW();
  if (!env) return out;
  
  wchar_t *curr = env;
  while (*curr) {
    std::wstring s(curr);
    size_t eq = s.find(L'=');
    if (eq != std::wstring::npos && eq > 0) {
      out.push_back({w2u8(s.substr(0, eq)), w2u8(s.substr(eq + 1))});
    }
    curr += s.length() + 1;
  }
  FreeEnvironmentStringsW(env);
  return out;
}

bool Win32Backend::env_set(const std::string &name, const std::string &value) {
  std::wstring wname(name.begin(), name.end());
  std::wstring wval(value.begin(), value.end());
  return SetEnvironmentVariableW(wname.c_str(), wval.c_str()) != FALSE;
}

std::vector<DriveInfo> Win32Backend::wine_get_drives() {
  std::vector<DriveInfo> out;
  DWORD mask = GetLogicalDrives();
  for (int i = 0; i < 26; ++i) {
    if (mask & (1 << i)) {
      char letter = 'A' + i;
      std::string root = std::string(1, letter) + ":\\";
      std::wstring wroot(root.begin(), root.end());
      
      DriveInfo di;
      di.letter = std::string(1, letter);
      di.mapping = ""; // default
      
      UINT type = GetDriveTypeW(wroot.c_str());
      switch(type) {
        case DRIVE_FIXED: di.type = "Fixed"; break;
        case DRIVE_REMOTE: di.type = "Remote"; break;
        case DRIVE_CDROM: di.type = "CDROM"; break;
        case DRIVE_RAMDISK: di.type = "RamDisk"; break;
        default: di.type = "Unknown"; break;
      }
      
      wchar_t buf[MAX_PATH];
      if (QueryDosDeviceW((std::wstring(1, (wchar_t)('A' + i)) + L":").c_str(), buf, MAX_PATH)) {
        di.mapping = w2u8(buf);
      }
      out.push_back(di);
    }
  }
  return out;
}

std::vector<std::string> Win32Backend::wine_get_overrides() {
  std::vector<std::string> out;
  // Read HKCU\Software\Wine\DllOverrides
  auto info = reg_read("HKCU\\Software\\Wine\\DllOverrides");
  if (info) {
    for (const auto &val : info->values) {
      out.push_back(val.name + "=" + val.data);
    }
  }
  return out;
}

// Sync
bool Win32Backend::sync_check_mutex(const std::string &name) {
  std::wstring wname(name.begin(), name.end());
  HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, wname.c_str());
  if (h) {
    CloseHandle(h);
    return true;
  }
  return false;
}

bool Win32Backend::sync_create_mutex(const std::string &name, bool own) {
  std::wstring wname(name.begin(), name.end());
  HANDLE h = CreateMutexW(NULL, own ? TRUE : FALSE, wname.c_str());
  if (h) {
    bool already = (GetLastError() == ERROR_ALREADY_EXISTS);
    CloseHandle(h);
    return !already;
  }
  return false;
}

// Advanced Automation
std::optional<MemoryRegion> Win32Backend::mem_read(uint32_t pid, uint64_t address, size_t size) {
  HANDLE h = OpenProcess(PROCESS_VM_READ, FALSE, pid);
  if (!h) return std::nullopt;

  std::vector<uint8_t> buffer(size);
  SIZE_T read;
  if (ReadProcessMemory(h, (LPCVOID)address, buffer.data(), size, &read)) {
    MemoryRegion mr;
    mr.address = address;
    buffer.resize(read);
    mr.data_b64 = base64_encode(buffer);
    CloseHandle(h);
    return mr;
  }
  CloseHandle(h);
  return std::nullopt;
}

bool Win32Backend::mem_write(uint32_t pid, uint64_t address, const std::vector<uint8_t> &data) {
  HANDLE h = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
  if (!h) return false;

  SIZE_T written;
  bool ok = WriteProcessMemory(h, (LPVOID)address, data.data(), data.size(), &written) != FALSE;
  CloseHandle(h);
  return ok;
}

std::optional<ImageMatchResult> Win32Backend::image_match(Rect region, const std::vector<uint8_t> &sub_image_bmp) {
  // ... existing image_match stub ...
  return std::nullopt; 
}

bool Win32Backend::input_hook_enable(bool) {
  // Global hooks require a message loop. 
  // This will be wired to the daemon's poll cycle.
  return true;
}

EnsureResult Win32Backend::ensure_visible(hwnd_u64 hwnd, bool visible) {
  HWND h = from_u64(hwnd);
  if (!IsWindow(h))
    return {false};
  bool cur = IsWindowVisible(h) != FALSE;
  if (cur == visible)
    return {false};
  ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
  return {true};
}

EnsureResult Win32Backend::ensure_foreground(hwnd_u64 hwnd) {
  HWND h = from_u64(hwnd);
  if (!IsWindow(h))
    return {false};
  HWND fg = GetForegroundWindow();
  if (fg == h)
    return {false};
  SetForegroundWindow(h);
  return {true};
}

bool Win32Backend::post_message(hwnd_u64 hwnd, uint32_t msg, uint64_t wparam,
                                uint64_t lparam) {
  HWND h = from_u64(hwnd);
  if (!IsWindow(h))
    return false;
  return PostMessageW(h, msg, (WPARAM)wparam, (LPARAM)lparam) != FALSE;
}

bool Win32Backend::send_input(const std::vector<uint8_t> &raw_input_data) {
  if (raw_input_data.empty())
    return false;
  // Assumes raw_input_data is a tightly packed array of INPUT structures
  if (raw_input_data.size() % sizeof(INPUT) != 0)
    return false;
  UINT count = (UINT)(raw_input_data.size() / sizeof(INPUT));
  const INPUT *pInputs = reinterpret_cast<const INPUT *>(raw_input_data.data());
  return SendInput(count, const_cast<INPUT *>(pInputs), sizeof(INPUT)) == count;
}

bool Win32Backend::send_mouse_click(int x, int y, int button) {
  // 0=left, 1=right, 2=middle
  // Use absolute coordinates
  int sw = GetSystemMetrics(SM_CXSCREEN);
  int sh = GetSystemMetrics(SM_CYSCREEN);
  if (sw == 0)
    sw = 1;
  if (sh == 0)
    sh = 1;

  // Normalize to 0-65535
  int nx = (x * 65535) / sw;
  int ny = (y * 65535) / sh;

  INPUT inputs[2] = {};
  inputs[0].type = INPUT_MOUSE;
  inputs[0].mi.dx = nx;
  inputs[0].mi.dy = ny;
  inputs[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;

  if (button == 0) {
    inputs[0].mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
    inputs[1] = inputs[0];
    inputs[1].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP;
  } else if (button == 1) {
    inputs[0].mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
    inputs[1] = inputs[0];
    inputs[1].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_RIGHTUP;
  } else if (button == 2) {
    inputs[0].mi.dwFlags |= MOUSEEVENTF_MIDDLEDOWN;
    inputs[1] = inputs[0];
    inputs[1].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MIDDLEUP;
  } else {
    return false;
  }

  // Send click
  return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

bool Win32Backend::send_key_press(int vk) {
  INPUT inputs[2] = {};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = (WORD)vk;

  inputs[1] = inputs[0];
  inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

  return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

bool Win32Backend::send_text(const std::string &text) {
  std::wstring wtext;
  int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
  if (len > 0) {
    wtext.resize(len - 1);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), len);
  }

  if (wtext.empty())
    return true;

  std::vector<INPUT> inputs;
  inputs.reserve(wtext.size() * 2);

  for (wchar_t c : wtext) {
    INPUT i = {};
    i.type = INPUT_KEYBOARD;
    i.ki.wScan = c;
    i.ki.dwFlags = KEYEVENTF_UNICODE;
    inputs.push_back(i);

    i.ki.dwFlags |= KEYEVENTF_KEYUP;
    inputs.push_back(i);
  }

  return SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT)) ==
         inputs.size();
}

static UIElementInfo get_element_info(IUIAutomationElement *pNode) {
  UIElementInfo info;
  BSTR bStr = NULL;
  if (SUCCEEDED(pNode->get_CurrentAutomationId(&bStr))) {
    info.automation_id = bstr_to_utf8(bStr);
    SysFreeString(bStr);
  }
  if (SUCCEEDED(pNode->get_CurrentName(&bStr))) {
    info.name = bstr_to_utf8(bStr);
    SysFreeString(bStr);
  }
  if (SUCCEEDED(pNode->get_CurrentClassName(&bStr))) {
    info.class_name = bstr_to_utf8(bStr);
    SysFreeString(bStr);
  }
  CONTROLTYPEID cType;
  if (SUCCEEDED(pNode->get_CurrentControlType(&cType))) {
    info.control_type = std::to_string(cType);
  }
  RECT r = {};
  if (SUCCEEDED(pNode->get_CurrentBoundingRectangle(&r))) {
    info.bounding_rect = {r.left, r.top, r.right, r.bottom};
  }
  BOOL bVal = FALSE;
  if (SUCCEEDED(pNode->get_CurrentIsEnabled(&bVal)))
    info.enabled = bVal;
  if (SUCCEEDED(pNode->get_CurrentIsOffscreen(&bVal)))
    info.visible = !bVal;
  return info;
}

static void walk_uia_tree(IUIAutomation *pAutomation, IUIAutomationElement *pRoot,
                          std::vector<UIElementInfo> &results, int depth) {
  if (depth > 5)
    return;

  ComPtr<IUIAutomationCondition> pTrueCondition;
  pAutomation->CreateTrueCondition(&pTrueCondition);
  ComPtr<IUIAutomationElementArray> pChildren;
  if (pTrueCondition) {
    pRoot->FindAll(TreeScope_Children, pTrueCondition, &pChildren);
  }

  if (pChildren) {
    int length = 0;
    pChildren->get_Length(&length);
    for (int i = 0; i < length; i++) {
      ComPtr<IUIAutomationElement> pNode;
      if (SUCCEEDED(pChildren->GetElement(i, &pNode)) && pNode) {
        UIElementInfo info = get_element_info(pNode);
        walk_uia_tree(pAutomation, pNode, info.children, depth + 1);
        results.push_back(info);
      }
    }
  }
}

std::vector<UIElementInfo> Win32Backend::inspect_ui_elements(hwnd_u64 parent) {
  std::vector<UIElementInfo> results;
  ComPtr<IUIAutomation> pAutomation;
  HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                                IID_IUIAutomation, (void **)&pAutomation);
  if (FAILED(hr)) {
    return results;
  }

  ComPtr<IUIAutomationElement> pRoot;
  HWND hParent = from_u64(parent);
  if (IsWindow(hParent)) {
    hr = pAutomation->ElementFromHandle(hParent, &pRoot);
  }

  if (SUCCEEDED(hr) && pRoot) {
    walk_uia_tree(pAutomation, pRoot, results, 0);
  }

  return results;
}

bool Win32Backend::invoke_ui_element(hwnd_u64 hwnd,
                                     const std::string &automation_id) {
  ComPtr<IUIAutomation> pAutomation;
  HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                                IID_IUIAutomation, (void **)&pAutomation);
  if (FAILED(hr)) return false;

  bool success = false;
  ComPtr<IUIAutomationElement> pRoot;
  HWND hParent = from_u64(hwnd);
  if (IsWindow(hParent)) {
    hr = pAutomation->ElementFromHandle(hParent, &pRoot);
  }

  if (SUCCEEDED(hr) && pRoot) {
    VARIANT varProp;
    varProp.vt = VT_BSTR;
    std::wstring wid = std::wstring(automation_id.begin(), automation_id.end());
    varProp.bstrVal = SysAllocString(wid.c_str());

    ComPtr<IUIAutomationCondition> pCondition;
    pAutomation->CreatePropertyCondition(UIA_AutomationIdPropertyId, varProp,
                                         &pCondition);
    ComPtr<IUIAutomationElement> pTarget;
    if (pCondition) {
      pRoot->FindFirst(TreeScope_Subtree, pCondition, &pTarget);
    }
    VariantClear(&varProp);

    if (pTarget) {
      ComPtr<IUIAutomationInvokePattern> pInvoke;
      if (SUCCEEDED(pTarget->GetCurrentPattern(UIA_InvokePatternId,
                                               (IUnknown **)&pInvoke)) &&
          pInvoke) {
        if (SUCCEEDED(pInvoke->Invoke())) {
          success = true;
        }
      }
    }
  }

  return success;
}

static std::vector<hwnd_u64> sorted(std::vector<hwnd_u64> v) {
  std::sort(v.begin(), v.end());
  return v;
}

json::Object Win32Backend::get_env_metadata() {
  json::Object o;
  o["os"] = "windows";
  
  // Detect Wine
  HMODULE hntdll = GetModuleHandleW(L"ntdll.dll");
  if (hntdll && GetProcAddress(hntdll, "wine_get_version")) {
    o["is_wine"] = true;
    typedef const char *(*p_wine_get_version)(void);
    auto p_version = (p_wine_get_version)GetProcAddress(hntdll, "wine_get_version");
    if (p_version) o["wine_version"] = std::string(p_version());
  } else {
    o["is_wine"] = false;
  }

#ifdef _WIN64
  o["arch"] = "x64";
#else
  o["arch"] = "x86";
#endif

  return o;
}

std::vector<Event> Win32Backend::poll_events(const Snapshot &old_snap,
                                             const Snapshot &new_snap) {
  std::vector<Event> out;
  auto o = sorted(old_snap.top);
  auto n = sorted(new_snap.top);

  std::vector<hwnd_u64> created;
  std::set_difference(n.begin(), n.end(), o.begin(), o.end(),
                      std::back_inserter(created));
  for (auto h : created)
    out.push_back({"window.created", h, ""});

  std::vector<hwnd_u64> destroyed;
  std::set_difference(o.begin(), o.end(), n.begin(), n.end(),
                      std::back_inserter(destroyed));
  for (auto h : destroyed)
    out.push_back({"window.destroyed", h, ""});

  return out;
}

} // namespace wininspect
#else
namespace wininspect {
Snapshot Win32Backend::capture_snapshot() { return {}; }
std::vector<hwnd_u64> Win32Backend::list_top(const Snapshot &s) {
  return s.top;
}
std::vector<hwnd_u64> Win32Backend::list_children(const Snapshot &, hwnd_u64) {
  return {};
}
std::optional<WindowInfo> Win32Backend::get_info(const Snapshot &, hwnd_u64) {
  return std::nullopt;
}
std::optional<hwnd_u64> Win32Backend::pick_at_point(const Snapshot &, int, int,
                                                    PickFlags) {
  return std::nullopt;
}
EnsureResult Win32Backend::ensure_visible(hwnd_u64, bool) { return {false}; }
EnsureResult Win32Backend::ensure_foreground(hwnd_u64) { return {false}; }
bool Win32Backend::post_message(hwnd_u64, uint32_t, uint64_t, uint64_t) {
  return false;
}
bool Win32Backend::send_input(const std::vector<uint8_t> &) { return false; }

bool Win32Backend::send_mouse_click(int, int, int) { return false; }
bool Win32Backend::send_key_press(int) { return false; }
bool Win32Backend::send_text(const std::string &) { return false; }
std::vector<UIElementInfo> Win32Backend::inspect_ui_elements(hwnd_u64) {
  return {};
}

std::vector<Event> Win32Backend::poll_events(const Snapshot &,
                                             const Snapshot &) {
  return {};
}
} // namespace wininspect
#endif
