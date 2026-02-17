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

  EnsureResult ensure_visible(hwnd_u64 hwnd, bool visible) override;
  EnsureResult ensure_foreground(hwnd_u64 hwnd) override;

  bool post_message(hwnd_u64 hwnd, uint32_t msg, uint64_t wparam,
                    uint64_t lparam) override;
  bool send_input(const std::vector<uint8_t> &raw_input_data) override;

  std::vector<Event> poll_events(const Snapshot &old_snap,
                                 const Snapshot &new_snap) override;
};

} // namespace wininspect
