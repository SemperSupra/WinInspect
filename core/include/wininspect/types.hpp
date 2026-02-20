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

struct Color {
  uint8_t r{}, g{}, b{};
  std::string to_hex() const;
};

struct ScreenCapture {
  int width{}, height{};
  std::string data_b64; // Base64 encoded BMP data
};

struct ProcessInfo {
  uint32_t pid{};
  std::string name;
  std::string path;
};

struct FileInfo {
  std::string path;
  uint64_t size{};
  bool is_directory{};
  std::string last_modified;
};

struct RegistryValue {
  std::string name;
  std::string type; // "SZ", "DWORD", "BINARY", "MULTI_SZ"
  std::string data; // Hex string for binary, UTF-8 for strings
};

struct RegistryKeyInfo {
  std::string path;
  std::vector<std::string> subkeys;
  std::vector<RegistryValue> values;
};

struct ServiceInfo {
  std::string name;
  std::string display_name;
  std::string state; // "RUNNING", "STOPPED", etc.
};

struct DriveInfo {
  std::string letter;
  std::string mapping; // Target path (e.g., / or C:\)
  std::string type; // "Fixed", "Remote", "CDROM", "RamDisk"
};

struct EnvVar {
  std::string name;
  std::string value;
};

struct MemoryRegion {
  uint64_t address{};
  std::string data_b64;
};

struct ImageMatchResult {
  int x{}, y{};
  double confidence{};
};

struct WindowInfo {
  hwnd_u64 hwnd{};
  hwnd_u64 parent{};
  hwnd_u64 owner{};
  std::string class_name;
  std::string title;
  Rect window_rect{};
  Rect client_rect{};
  Rect screen_rect{}; // Client coordinates in screen space
  std::uint32_t pid{};
  std::uint32_t tid{};
  std::uint64_t style{};
  std::uint64_t exstyle{};
  std::vector<std::string> style_flags;
  std::vector<std::string> ex_style_flags;
  bool visible{};
  bool enabled{};
  bool iconic{};
  bool zoomed{};
  std::string process_image;
};

struct WindowNode {
  hwnd_u64 hwnd{};
  std::string title;
  std::string class_name;
  std::vector<WindowNode> children;
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

struct UIElementInfo {
  std::string automation_id;
  std::string name;
  std::string class_name;
  std::string control_type;
  Rect bounding_rect{};
  bool enabled = false;
  bool visible = false;
  std::vector<UIElementInfo> children;
};

} // namespace wininspect
