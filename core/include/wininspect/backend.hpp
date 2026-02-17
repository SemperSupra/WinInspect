#pragma once
#include "types.hpp"
#include <optional>
#include <vector>

namespace wininspect {

struct EnsureResult {
  bool changed = false;
};

class IBackend {
public:
  virtual ~IBackend() = default;

  virtual Snapshot capture_snapshot() = 0;

  virtual std::vector<hwnd_u64> list_top(const Snapshot &s) = 0;
  virtual std::vector<hwnd_u64> list_children(const Snapshot &s,
                                              hwnd_u64 parent) = 0;
  virtual std::optional<WindowInfo> get_info(const Snapshot &s,
                                             hwnd_u64 hwnd) = 0;
  virtual std::optional<hwnd_u64> pick_at_point(const Snapshot &s, int x, int y,
                                                PickFlags flags) = 0;

  // Desired-state actions (may be no-op in some environments)
  virtual EnsureResult ensure_visible(hwnd_u64 hwnd, bool visible) = 0;
  virtual EnsureResult ensure_foreground(hwnd_u64 hwnd) = 0;

  // Event injection
  virtual bool post_message(hwnd_u64 hwnd, uint32_t msg, uint64_t wparam,
                            uint64_t lparam) = 0;
  virtual bool send_input(const std::vector<uint8_t> &raw_input_data) = 0;

  // Event polling
  virtual std::vector<Event> poll_events(const Snapshot &old_snap,
                                         const Snapshot &new_snap) = 0;
};

} // namespace wininspect
