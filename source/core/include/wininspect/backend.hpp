#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "types.hpp"
#include "update.hpp"
#include "tinyjson.hpp"
#include <optional>
#include <vector>

namespace wininspect {

  struct EnsureResult
  {
    bool changed = false;
  };

  class IBackend
  {
  public:
    virtual ~IBackend() = default;

    virtual void set_config(const json::Object& config) = 0;

    [[nodiscard]] virtual Snapshot capture_snapshot() = 0;

    [[nodiscard]] virtual std::vector<hwnd_u64> list_top(const Snapshot& s) = 0;
    [[nodiscard]] virtual std::vector<hwnd_u64> list_children(const Snapshot& s,
                                                              hwnd_u64 parent) = 0;
    [[nodiscard]] virtual std::optional<WindowInfo> get_info(const Snapshot& s, hwnd_u64 hwnd) = 0;
    [[nodiscard]] virtual std::optional<hwnd_u64> pick_at_point(const Snapshot& s, int x, int y,
                                                                PickFlags flags) = 0;

    [[nodiscard]] virtual std::vector<WindowNode> get_window_tree(const Snapshot& s,
                                                                  hwnd_u64 root) = 0;
    [[nodiscard]] virtual std::optional<std::pair<int, int>> get_z_order(hwnd_u64 hwnd) = 0;

    // Desired-state actions (may be no-op in some environments)
    [[nodiscard]] virtual bool move_window(hwnd_u64 hwnd, int x, int y) = 0;
    [[nodiscard]] virtual bool resize_window(hwnd_u64 hwnd, int width, int height) = 0;
    [[nodiscard]] virtual EnsureResult ensure_visible(hwnd_u64 hwnd, bool visible) = 0;
    [[nodiscard]] virtual EnsureResult ensure_foreground(hwnd_u64 hwnd) = 0;
    [[nodiscard]] virtual bool highlight_window(hwnd_u64 hwnd) = 0;
    [[nodiscard]] virtual bool set_property(hwnd_u64 hwnd, const std::string& name,
                                            const std::string& value) = 0;

    // Event injection
    [[nodiscard]] virtual bool post_message(hwnd_u64 hwnd, uint32_t msg, uint64_t wparam,
                                            uint64_t lparam) = 0;
    [[nodiscard]] virtual bool send_input(const std::vector<uint8_t>& raw_input_data) = 0;

    // Higher-level injection helpers
    [[nodiscard]] virtual bool send_mouse_click(int x, int y,
                                                int button) = 0; // 0=left, 1=right, 2=middle
    [[nodiscard]] virtual bool mouse_drag(int sx, int sy, int ex, int ey, int button,
                                          int duration_ms) = 0;
    [[nodiscard]] virtual bool send_key_press(int vk) = 0;
    [[nodiscard]] virtual bool send_text(const std::string& text) = 0;
    [[nodiscard]] virtual bool send_hotkey(const std::string& keys) = 0;

    // Stealth input (background)
    [[nodiscard]] virtual bool control_click(hwnd_u64 hwnd, int x, int y, int button) = 0;
    [[nodiscard]] virtual bool control_send(hwnd_u64 hwnd, const std::string& text) = 0;

    // Visuals
    [[nodiscard]] virtual std::optional<Color> get_pixel(int x, int y) = 0;
    [[nodiscard]] virtual std::optional<ScreenCapture> capture_screen(Rect region) = 0;
    [[nodiscard]] virtual std::optional<std::pair<int, int>> pixel_search(Rect region, Color target,
                                                                          int variation) = 0;
    [[nodiscard]] virtual DesktopInfo get_desktop_info() = 0;

    // Process Management
    [[nodiscard]] virtual std::vector<ProcessInfo> list_processes() = 0;
    [[nodiscard]] virtual bool kill_process(uint32_t pid) = 0;
    [[nodiscard]] virtual ProcessExecResult execute_process(const std::string& cmd,
                                                            const std::string& args) = 0;

    // File System
    [[nodiscard]] virtual std::optional<FileInfo> get_file_info(const std::string& path) = 0;
    [[nodiscard]] virtual std::optional<std::string> read_file_content(const std::string& path) = 0;

    // Advanced Discovery
    [[nodiscard]] virtual std::vector<hwnd_u64>
    find_windows_regex(const std::string& title_regex, const std::string& class_regex) = 0;

    // Registry Management
    [[nodiscard]] virtual std::optional<RegistryKeyInfo> reg_read(const std::string& path) = 0;
    [[nodiscard]] virtual bool reg_write(const std::string& path, const RegistryValue& val) = 0;
    [[nodiscard]] virtual bool
    reg_delete(const std::string& path,
               const std::string& value_name) = 0; // value_name empty = delete key
    [[nodiscard]] virtual bool reg_subscribe(const std::string& path) = 0;

    // Clipboard
    [[nodiscard]] virtual std::optional<std::string> clipboard_read() = 0;
    [[nodiscard]] virtual bool clipboard_write(const std::string& text) = 0;

    // Services
    [[nodiscard]] virtual std::vector<ServiceInfo> service_list() = 0;
    [[nodiscard]] virtual std::string service_status(const std::string& name) = 0;
    [[nodiscard]] virtual bool service_control(const std::string& name,
                                               const std::string& action) = 0; // start, stop

    // Wine/System Environment
    [[nodiscard]] virtual std::vector<EnvVar> env_get_all() = 0;
    [[nodiscard]] virtual bool env_set(const std::string& name, const std::string& value) = 0;
    [[nodiscard]] virtual std::vector<DriveInfo> wine_get_drives() = 0;
    [[nodiscard]] virtual std::vector<std::string> wine_get_overrides() = 0;

    // Advanced Sync
    [[nodiscard]] virtual bool sync_check_mutex(const std::string& name) = 0;
    [[nodiscard]] virtual bool sync_create_mutex(const std::string& name, bool own) = 0;

    // Advanced Automation (Final Frontier)
    [[nodiscard]] virtual std::optional<MemoryRegion> mem_read(uint32_t pid, uint64_t address,
                                                               size_t size) = 0;
    [[nodiscard]] virtual bool mem_write(uint32_t pid, uint64_t address,
                                         const std::vector<uint8_t>& data) = 0;
    [[nodiscard]] virtual std::optional<ImageMatchResult>
    image_match(Rect region, const std::vector<uint8_t>& sub_image_bmp) = 0;
    [[nodiscard]] virtual bool input_hook_enable(bool enabled) = 0;

    // UI Automation
    [[nodiscard]] virtual std::vector<UIElementInfo> inspect_ui_elements(hwnd_u64 parent) = 0;
    [[nodiscard]] virtual bool invoke_ui_element(hwnd_u64 hwnd,
                                                 const std::string& automation_id) = 0;

    [[nodiscard]] virtual Capabilities get_capabilities() = 0;
    [[nodiscard]] virtual json::Object get_env_metadata() = 0;
    [[nodiscard]] virtual InstanceIdentity get_instance_identity() = 0;

    // Auto-update
    [[nodiscard]] virtual update::UpdateInfo check_for_update() = 0;
    [[nodiscard]] virtual std::string download_update(const std::string& url,
                                                      const std::string& type_hint) = 0;

    // Event polling
    [[nodiscard]] virtual std::vector<Event> poll_events(const Snapshot& old_snap,
                                                         const Snapshot& new_snap) = 0;

    /// Poll for local human input (keyboard/mouse activity since last call).
    /// Returns true if input was detected since the previous poll.
    [[nodiscard]] virtual bool poll_local_input() = 0;

    // Daemon lifecycle state (for diagnostics)
    [[nodiscard]] virtual std::string daemon_state() const { return "running"; }

    // Session recording
    [[nodiscard]] virtual bool start_recording(const std::string& path, int interval_ms,
                                               int max_frames, bool record_input) = 0;
    [[nodiscard]] virtual bool stop_recording() = 0;
  };

} // namespace wininspect
