#include "wininspect/fake_backend.hpp"
#include <algorithm>

namespace wininspect {

FakeBackend::FakeBackend(std::vector<FakeWindow> windows) {
  for (auto &w : windows)
    w_.emplace(w.hwnd, std::move(w));
}

Snapshot FakeBackend::capture_snapshot() {
  Snapshot s;
  // stable ordering by hwnd
  for (const auto &[hwnd, w] : w_) {
    if (w.parent == 0)
      s.top.push_back(hwnd);
  }
  std::sort(s.top.begin(), s.top.end());
  return s;
}

std::vector<hwnd_u64> FakeBackend::list_top(const Snapshot &s) { return s.top; }

std::vector<hwnd_u64> FakeBackend::list_children(const Snapshot &,
                                                 hwnd_u64 parent) {
  std::vector<hwnd_u64> out;
  for (const auto &[hwnd, w] : w_)
    if (w.parent == parent)
      out.push_back(hwnd);
  std::sort(out.begin(), out.end());
  return out;
}

std::optional<WindowInfo> FakeBackend::get_info(const Snapshot &,
                                                hwnd_u64 hwnd) {
  auto it = w_.find(hwnd);
  if (it == w_.end())
    return std::nullopt;
  const auto &fw = it->second;

  WindowInfo wi{};
  wi.hwnd = fw.hwnd;
  wi.parent = fw.parent;
  wi.owner = fw.owner;
  wi.class_name = fw.cls;
  wi.title = fw.title;
  wi.window_rect = {0, 0, 100, 100};
  wi.client_rect = {0, 0, 100, 100};
  wi.pid = 1234;
  wi.tid = 5678;
  wi.style = 0;
  wi.exstyle = 0;
  wi.visible = fw.visible;
  wi.enabled = true;
  wi.iconic = false;
  wi.zoomed = false;
  wi.process_image = "fake.exe";
  return wi;
}

std::optional<hwnd_u64> FakeBackend::pick_at_point(const Snapshot &, int, int,
                                                   PickFlags) {
  // Deterministic: pick smallest top window hwnd
  auto s = capture_snapshot();
  if (s.top.empty())
    return std::nullopt;
  return s.top.front();
}

EnsureResult FakeBackend::ensure_visible(hwnd_u64 hwnd, bool visible) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = w_.find(hwnd);
  if (it == w_.end())
    return {false};
  bool changed = (it->second.visible != visible);
  it->second.visible = visible;
  return {changed};
}

EnsureResult FakeBackend::ensure_foreground(hwnd_u64 hwnd) {
  std::lock_guard<std::mutex> lk(mu_);
  bool changed = (foreground_ != hwnd);
  foreground_ = hwnd;
  return {changed};
}

bool FakeBackend::post_message(hwnd_u64, uint32_t, uint64_t, uint64_t) {
  return true; // Mock success
}

bool FakeBackend::send_input(const std::vector<uint8_t> &) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("send_input");
  return true;
}

bool FakeBackend::send_mouse_click(int x, int y, int button) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("mouse_click:" + std::to_string(x) + "," +
                             std::to_string(y) + "," + std::to_string(button));
  return true;
}

bool FakeBackend::send_key_press(int vk) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("key_press:" + std::to_string(vk));
  return true;
}

bool FakeBackend::send_text(const std::string &text) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("text:" + text);
  return true;
}

std::vector<UIElementInfo> FakeBackend::inspect_ui_elements(hwnd_u64 parent) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = ui_elements_.find(parent);
  if (it == ui_elements_.end())
    return {};
  return it->second;
}

void FakeBackend::add_fake_ui_element(hwnd_u64 parent,
                                      const UIElementInfo &info) {
  std::lock_guard<std::mutex> lk(mu_);
  ui_elements_[parent].push_back(info);
}

std::vector<std::string> FakeBackend::get_injected_events() const {
  std::lock_guard<std::mutex> lk(mu_);
  return injected_events_;
}

void FakeBackend::clear_injected_events() {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.clear();
}

std::vector<Event> FakeBackend::poll_events(const Snapshot &,
                                            const Snapshot &) {
  return {}; // Placeholder
}

} // namespace wininspect
