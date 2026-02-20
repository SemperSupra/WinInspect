#pragma once
#include "backend.hpp"

namespace wininspect {

class Win32Backend final : public IBackend {
public:
  Snapshot capture_snapshot() override;

  std::vector<hwnd_u64> list_top(const Snapshot &s) override;
  std::vector<hwnd_u64> list_children(const Snapshot &s,
                                      hwnd_u64 parent) override;
  std::optional<WindowInfo> get_info(const Snapshot &s, hwnd_u64 hwnd) override;
  std::optional<hwnd_u64> pick_at_point(const Snapshot &s, int x, int y,
                                        PickFlags flags) override;

  std::vector<WindowNode> get_window_tree(const Snapshot &s, hwnd_u64 root) override;

  EnsureResult ensure_visible(hwnd_u64 hwnd, bool visible) override;
  EnsureResult ensure_foreground(hwnd_u64 hwnd) override;
  bool highlight_window(hwnd_u64 hwnd) override;
  bool set_property(hwnd_u64 hwnd, const std::string &name, const std::string &value) override;

  bool post_message(hwnd_u64 hwnd, uint32_t msg, uint64_t wparam,
                    uint64_t lparam) override;
  bool send_input(const std::vector<uint8_t> &raw_input_data) override;

  bool send_mouse_click(int x, int y, int button) override;
  bool send_key_press(int vk) override;
  bool send_text(const std::string &text) override;

  bool control_click(hwnd_u64 hwnd, int x, int y, int button) override;
  bool control_send(hwnd_u64 hwnd, const std::string &text) override;

  std::optional<Color> get_pixel(int x, int y) override;
  std::optional<ScreenCapture> capture_screen(Rect region) override;
  std::optional<std::pair<int, int>> pixel_search(Rect region, Color target, int variation) override;

  // Process Management
  std::vector<ProcessInfo> list_processes() override;
  bool kill_process(uint32_t pid) override;

  // File System
  std::optional<FileInfo> get_file_info(const std::string &path) override;
  std::optional<std::string> read_file_content(const std::string &path) override;

  // Advanced Discovery
  std::vector<hwnd_u64> find_windows_regex(const std::string &title_regex, const std::string &class_regex) override;

  // Registry Management
  std::optional<RegistryKeyInfo> reg_read(const std::string &path) override;
  bool reg_write(const std::string &path, const RegistryValue &val) override;
  bool reg_delete(const std::string &path, const std::string &value_name) override;
  bool reg_subscribe(const std::string &path) override;

  std::optional<std::string> clipboard_read() override;
  bool clipboard_write(const std::string &text) override;

  std::vector<ServiceInfo> service_list() override;
  std::string service_status(const std::string &name) override;
  bool service_control(const std::string &name, const std::string &action) override;

  std::vector<EnvVar> env_get_all() override;
  bool env_set(const std::string &name, const std::string &value) override;
  std::vector<DriveInfo> wine_get_drives() override;
  std::vector<std::string> wine_get_overrides() override;

  bool sync_check_mutex(const std::string &name) override;
  bool sync_create_mutex(const std::string &name, bool own) override;

  std::optional<MemoryRegion> mem_read(uint32_t pid, uint64_t address, size_t size) override;
  bool mem_write(uint32_t pid, uint64_t address, const std::vector<uint8_t> &data) override;
  std::optional<ImageMatchResult> image_match(Rect region, const std::vector<uint8_t> &sub_image_bmp) override;
  bool input_hook_enable(bool enabled) override;

  std::vector<UIElementInfo> inspect_ui_elements(hwnd_u64 parent) override;
  bool invoke_ui_element(hwnd_u64 hwnd, const std::string &automation_id) override;

  json::Object get_env_metadata() override;

  std::vector<Event> poll_events(const Snapshot &old_snap,
                                 const Snapshot &new_snap) override;
};

} // namespace wininspect
