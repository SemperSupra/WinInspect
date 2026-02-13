#include "wininspect/win32_backend.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <vector>
#include <string>

namespace wininspect {

static hwnd_u64 to_u64(HWND h) { return static_cast<hwnd_u64>(reinterpret_cast<std::uintptr_t>(h)); }
static HWND from_u64(hwnd_u64 h) { return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(h)); }

static std::string w2u8(const std::wstring& ws) {
  if (ws.empty()) return {};
  int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
  std::string out(len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), len, nullptr, nullptr);
  return out;
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
  if (!h) return out;
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
  EnumWindows([](HWND h, LPARAM lp) -> BOOL {
    auto* vec = reinterpret_cast<std::vector<hwnd_u64>*>(lp);
    vec->push_back(to_u64(h));
    return TRUE;
  }, reinterpret_cast<LPARAM>(&s.top));
  return s;
}

std::vector<hwnd_u64> Win32Backend::list_top(const Snapshot& s) { return s.top; }

std::vector<hwnd_u64> Win32Backend::list_children(const Snapshot&, hwnd_u64 parent) {
  std::vector<hwnd_u64> out;
  EnumChildWindows(from_u64(parent), [](HWND h, LPARAM lp) -> BOOL {
    auto* vec = reinterpret_cast<std::vector<hwnd_u64>*>(lp);
    vec->push_back(to_u64(h));
    return TRUE;
  }, reinterpret_cast<LPARAM>(&out));
  return out;
}

std::optional<WindowInfo> Win32Backend::get_info(const Snapshot&, hwnd_u64 hwnd_u) {
  HWND hwnd = from_u64(hwnd_u);
  if (!IsWindow(hwnd)) return std::nullopt;

  WindowInfo wi{};
  wi.hwnd = hwnd_u;
  wi.parent = to_u64(GetParent(hwnd));
  wi.owner  = to_u64(GetWindow(hwnd, GW_OWNER));
  wi.class_name = w2u8(get_class_name_w(hwnd));
  wi.title      = w2u8(get_window_text_w(hwnd));

  RECT r{};
  GetWindowRect(hwnd, &r);
  wi.window_rect = {r.left, r.top, r.right, r.bottom};

  RECT cr{};
  GetClientRect(hwnd, &cr);
  wi.client_rect = {cr.left, cr.top, cr.right, cr.bottom};

  DWORD pid=0;
  wi.tid = GetWindowThreadProcessId(hwnd, &pid);
  wi.pid = pid;

  LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  LONG_PTR exsty = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  wi.style = (std::uint64_t)(std::uintptr_t)style;
  wi.exstyle = (std::uint64_t)(std::uintptr_t)exsty;

  wi.visible = IsWindowVisible(hwnd) != FALSE;
  wi.enabled = IsWindowEnabled(hwnd) != FALSE;
  wi.iconic  = IsIconic(hwnd) != FALSE;
  wi.zoomed  = IsZoomed(hwnd) != FALSE;

  wi.process_image = try_process_image_path(pid);
  return wi;
}

std::optional<hwnd_u64> Win32Backend::pick_at_point(const Snapshot&, int x, int y, PickFlags flags) {
  POINT pt{x,y};
  HWND h = WindowFromPoint(pt);
  if (!h) return std::nullopt;

  if (flags.prefer_child) {
    HWND child = ChildWindowFromPointEx(h, pt, flags.ignore_transparent ? CWP_SKIPTRANSPARENT : 0);
    if (child) h = child;
  }
  return to_u64(h);
}

EnsureResult Win32Backend::ensure_visible(hwnd_u64 hwnd, bool visible) {
  HWND h = from_u64(hwnd);
  if (!IsWindow(h)) return {false};
  bool cur = IsWindowVisible(h) != FALSE;
  if (cur == visible) return {false};
  ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
  return {true};
}

EnsureResult Win32Backend::ensure_foreground(hwnd_u64 hwnd) {
  HWND h = from_u64(hwnd);
  if (!IsWindow(h)) return {false};
  HWND fg = GetForegroundWindow();
  if (fg == h) return {false};
  SetForegroundWindow(h);
  return {true};
}

bool Win32Backend::post_message(hwnd_u64 hwnd, uint32_t msg, uint64_t wparam, uint64_t lparam) {
  HWND h = from_u64(hwnd);
  if (!IsWindow(h)) return false;
  return PostMessageW(h, msg, (WPARAM)wparam, (LPARAM)lparam) != FALSE;
}

bool Win32Backend::send_input(const std::vector<uint8_t>& raw_input_data) {
  if (raw_input_data.empty()) return false;
  // Assumes raw_input_data is a tightly packed array of INPUT structures
  if (raw_input_data.size() % sizeof(INPUT) != 0) return false;
  UINT count = (UINT)(raw_input_data.size() / sizeof(INPUT));
  const INPUT* pInputs = reinterpret_cast<const INPUT*>(raw_input_data.data());
  return SendInput(count, const_cast<INPUT*>(pInputs), sizeof(INPUT)) == count;
}

static std::vector<hwnd_u64> sorted(std::vector<hwnd_u64> v) {
    std::sort(v.begin(), v.end());
    return v;
}

std::vector<Event> Win32Backend::poll_events(const Snapshot& old_snap, const Snapshot& new_snap) {
    std::vector<Event> out;
    auto o = sorted(old_snap.top);
    auto n = sorted(new_snap.top);

    std::vector<hwnd_u64> created;
    std::set_difference(n.begin(), n.end(), o.begin(), o.end(), std::back_inserter(created));
    for (auto h : created) out.push_back({"window.created", h, ""});

    std::vector<hwnd_u64> destroyed;
    std::set_difference(o.begin(), o.end(), n.begin(), n.end(), std::back_inserter(destroyed));
    for (auto h : destroyed) out.push_back({"window.destroyed", h, ""});

    return out;
}

} // namespace wininspect
#else
namespace wininspect {
Snapshot Win32Backend::capture_snapshot() { return {}; }
std::vector<hwnd_u64> Win32Backend::list_top(const Snapshot& s){ return s.top; }
std::vector<hwnd_u64> Win32Backend::list_children(const Snapshot&, hwnd_u64){ return {}; }
std::optional<WindowInfo> Win32Backend::get_info(const Snapshot&, hwnd_u64){ return std::nullopt; }
std::optional<hwnd_u64> Win32Backend::pick_at_point(const Snapshot&, int,int,PickFlags){ return std::nullopt; }
EnsureResult Win32Backend::ensure_visible(hwnd_u64, bool){ return {false}; }
EnsureResult Win32Backend::ensure_foreground(hwnd_u64){ return {false}; }
bool Win32Backend::post_message(hwnd_u64, uint32_t, uint64_t, uint64_t){ return false; }
bool Win32Backend::send_input(const std::vector<uint8_t>&){ return false; }
std::vector<Event> Win32Backend::poll_events(const Snapshot&, const Snapshot&){ return {}; }
}
#endif
