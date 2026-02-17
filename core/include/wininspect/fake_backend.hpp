#pragma once
#include "backend.hpp"
#include <map>
#include <mutex>

namespace wininspect {

struct FakeWindow {
  hwnd_u64 hwnd{};
  hwnd_u64 parent{};
  hwnd_u64 owner{};
  std::string title;
  std::string cls;
  bool visible = true;
};

class FakeBackend final : public IBackend {
public:
  explicit FakeBackend(std::vector<FakeWindow> windows);

  Snapshot capture_snapshot() override;

  std::vector<hwnd_u64> list_top(const Snapshot &s) override;
  std::vector<hwnd_u64> list_children(const Snapshot &s,
                                      hwnd_u64 parent) override;
  std::optional<WindowInfo> get_info(const Snapshot &s, hwnd_u64 hwnd) override;
  std::optional<hwnd_u64> pick_at_point(const Snapshot &s, int x, int y,
                                        PickFlags flags) override;

  EnsureResult ensure_visible(hwnd_u64 hwnd, bool visible) override;
  EnsureResult ensure_foreground(hwnd_u64 hwnd) override;

  bool post_message(hwnd_u64 hwnd, uint32_t msg, uint64_t wparam,
                    uint64_t lparam) override;
  bool send_input(const std::vector<uint8_t> &raw_input_data) override;

  bool send_mouse_click(int x, int y, int button) override;
  bool send_key_press(int vk) override;
  bool send_text(const std::string &text) override;

  std::vector<UIElementInfo> inspect_ui_elements(hwnd_u64 parent) override;

  // Test helpers
  void add_fake_ui_element(hwnd_u64 parent, const UIElementInfo &info);
  std::vector<std::string> get_injected_events() const;
  void clear_injected_events();

  std::vector<Event> poll_events(const Snapshot &old_snap,
                                 const Snapshot &new_snap) override;

private:
  mutable std::mutex mu_;
  std::map<hwnd_u64, FakeWindow> w_;
  hwnd_u64 foreground_ = 0;

  std::map<hwnd_u64, std::vector<UIElementInfo>> ui_elements_;
  std::vector<std::string> injected_events_;
};

} // namespace wininspect
