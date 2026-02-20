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

std::vector<WindowNode> FakeBackend::get_window_tree(const Snapshot &,
                                                   hwnd_u64 root_u) {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<WindowNode> results;

  auto build_node = [&](auto self, hwnd_u64 h) -> WindowNode {
    WindowNode n;
    n.hwnd = h;
    auto it = w_.find(h);
    if (it != w_.end()) {
      n.title = it->second.title;
      n.class_name = it->second.cls;
    }
    for (const auto &[child_h, child_w] : w_) {
      if (child_w.parent == h) {
        n.children.push_back(self(self, child_h));
      }
    }
    return n;
  };

  if (root_u == 0) {
    for (const auto &[h, w] : w_) {
      if (w.parent == 0) results.push_back(build_node(build_node, h));
    }
  } else {
    results.push_back(build_node(build_node, root_u));
  }
  return results;
}

bool FakeBackend::highlight_window(hwnd_u64 hwnd) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("highlight_window:" + std::to_string(hwnd));
  return true;
}

bool FakeBackend::set_property(hwnd_u64 hwnd, const std::string &name,
                                const std::string &value) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("set_property:" + name + "=" + value);
  return true;
}

bool FakeBackend::control_click(hwnd_u64 hwnd, int x, int y, int button) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("control_click:" + std::to_string(hwnd) + " at " +
                             std::to_string(x) + "," + std::to_string(y));
  return true;
}

bool FakeBackend::control_send(hwnd_u64 hwnd, const std::string &text) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("control_send:" + std::to_string(hwnd) + " text:" + text);
  return true;
}

std::optional<Color> FakeBackend::get_pixel(int, int) {
  return Color{255, 0, 0};
}

std::optional<ScreenCapture> FakeBackend::capture_screen(Rect) {
  return ScreenCapture{100, 100, "fake_b64"};
}

std::optional<std::pair<int, int>> FakeBackend::pixel_search(Rect, Color, int) {
  return std::make_pair(50, 50);
}

std::vector<ProcessInfo> FakeBackend::list_processes() {
  return {{1234, "fake.exe", "C:\\fake.exe"}};
}

bool FakeBackend::kill_process(uint32_t pid) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("kill_process:" + std::to_string(pid));
  return true;
}

std::optional<FileInfo> FakeBackend::get_file_info(const std::string &path) {
  return FileInfo{path, 1024, false, "2026-02-19"};
}

std::optional<std::string> FakeBackend::read_file_content(const std::string &) {
  return "fake content";
}

std::vector<hwnd_u64> FakeBackend::find_windows_regex(const std::string &,
                                                       const std::string &) {
  return {0x1234};
}

std::optional<RegistryKeyInfo> FakeBackend::reg_read(const std::string &path) {
  return RegistryKeyInfo{path, {"SubKey1"}, {{"TestValue", "SZ", "TestData"}}};
}

bool FakeBackend::reg_write(const std::string &path, const RegistryValue &val) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("reg_write:" + path + "\\" + val.name + "=" + val.data);
  return true;
}

bool FakeBackend::reg_delete(const std::string &path, const std::string &value_name) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("reg_delete:" + path + " val:" + value_name);
  return true;
}

bool FakeBackend::reg_subscribe(const std::string &path) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("reg_subscribe:" + path);
  return true;
}

std::optional<std::string> FakeBackend::clipboard_read() { return "fake clipboard"; }
bool FakeBackend::clipboard_write(const std::string &text) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("clipboard_write:" + text);
  return true;
}

std::vector<ServiceInfo> FakeBackend::service_list() { return {{"FakeSvc", "Fake Service", "RUNNING"}}; }
std::string FakeBackend::service_status(const std::string &) { return "RUNNING"; }
bool FakeBackend::service_control(const std::string &name, const std::string &action) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("service_control:" + name + " " + action);
  return true;
}

std::vector<EnvVar> FakeBackend::env_get_all() { return {{"PATH", "C:\\fake"}}; }
bool FakeBackend::env_set(const std::string &name, const std::string &value) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("env_set:" + name + "=" + value);
  return true;
}

std::vector<DriveInfo> FakeBackend::wine_get_drives() { return {{"C", "C:\\", "Fixed"}}; }
std::vector<std::string> FakeBackend::wine_get_overrides() { return {"d3d11=native"}; }

bool FakeBackend::sync_check_mutex(const std::string &) { return true; }
bool FakeBackend::sync_create_mutex(const std::string &name, bool) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("create_mutex:" + name);
  return true;
}

std::optional<MemoryRegion> FakeBackend::mem_read(uint32_t pid, uint64_t addr, size_t) {
  return MemoryRegion{addr, "ZmFrZSBtZW1vcnk="}; // "fake memory"
}

bool FakeBackend::mem_write(uint32_t pid, uint64_t addr, const std::vector<uint8_t> &) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("mem_write:" + std::to_string(pid) + "@" + std::to_string(addr));
  return true;
}

std::optional<ImageMatchResult> FakeBackend::image_match(Rect, const std::vector<uint8_t> &) {
  return ImageMatchResult{10, 10, 1.0};
}

bool FakeBackend::input_hook_enable(bool enabled) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back(std::string("input_hook:") + (enabled ? "on" : "off"));
  return true;
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

static bool find_and_invoke(const std::vector<UIElementInfo> &elements,
                            const std::string &id) {
  for (const auto &el : elements) {
    if (el.automation_id == id)
      return true;
    if (find_and_invoke(el.children, id))
      return true;
  }
  return false;
}

bool FakeBackend::invoke_ui_element(hwnd_u64 hwnd,
                                    const std::string &automation_id) {
  std::lock_guard<std::mutex> lk(mu_);
  injected_events_.push_back("invoke_ui_element:" + automation_id);
  auto it = ui_elements_.find(hwnd);
  if (it == ui_elements_.end())
    return false;
  return find_and_invoke(it->second, automation_id);
}

json::Object FakeBackend::get_env_metadata() {
  json::Object o;
  o["os"] = "fake_windows";
  o["is_wine"] = false;
  o["arch"] = "x64";
  return o;
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
