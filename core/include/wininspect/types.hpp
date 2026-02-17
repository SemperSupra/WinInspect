#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wininspect {

using hwnd_u64 = std::uint64_t;
inline constexpr std::string_view PROTOCOL_VERSION = "1.0.0";

struct Rect {
  long left{}, top{}, right{}, bottom{};
};

struct WindowInfo {
  hwnd_u64 hwnd{};
  hwnd_u64 parent{};
  hwnd_u64 owner{};
  std::string class_name;
  std::string title;
  Rect window_rect{};
  Rect client_rect{};
  std::uint32_t pid{};
  std::uint32_t tid{};
  std::uint64_t style{};
  std::uint64_t exstyle{};
  bool visible{};
  bool enabled{};
  bool iconic{};
  bool zoomed{};
  std::string process_image;
};

struct PickFlags {
  bool prefer_child = true;
  bool ignore_transparent = true;
};

struct Snapshot {
  // Minimal snapshot for v1: stable list of top windows and their metadata.
  // Real implementations can expand this.
  std::vector<hwnd_u64> top;
};

struct Event {
  std::string type; // "window.created", "window.destroyed", "window.changed"
  hwnd_u64 hwnd{};
  std::string property; // for "window.changed"
};

} // namespace wininspect
