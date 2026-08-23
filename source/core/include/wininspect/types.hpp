#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include <cstdint>
#include <string>
#include <vector>
#include "tinyjson.hpp"

namespace wininspect {

  using hwnd_u64 = std::uint64_t;

  struct Hwnd
  {
    hwnd_u64 val{};
    explicit Hwnd(hwnd_u64 h = 0) : val(h) {}
    bool operator==(const Hwnd& other) const { return val == other.val; }
    bool operator<(const Hwnd& other) const { return val < other.val; }
    bool is_valid() const { return val != 0; }
    std::string to_string() const;
  };

  struct SessionID
  {
    std::string val;
    explicit SessionID(const std::string& s = "") : val(s) {}
    bool empty() const { return val.empty(); }
  };

  inline constexpr std::string_view PROTOCOL_VERSION = "0.3.0";
  inline constexpr std::string_view WININSPECT_VERSION = "v0.4.0";

  // ── Protocol and Resource Limits ──────────────────────────────────────────
  inline constexpr size_t MAX_MESSAGE_SIZE = 10 * 1024 * 1024; // max request/response payload
  inline constexpr int SELECT_POLL_US = 100000;                // select() poll interval (100ms)
  inline constexpr int PIPE_BUFFER_SIZE = 64 * 1024;           // named pipe buffer

  // ── Compile-time invariant checks ─────────────────────────────────────────
  static_assert(MAX_MESSAGE_SIZE > 0 && MAX_MESSAGE_SIZE <= 1024 * 1024 * 1024,
                "MAX_MESSAGE_SIZE must be between 1 and 1 GB");
  static_assert(SELECT_POLL_US > 0 && SELECT_POLL_US <= 60000000,
                "SELECT_POLL_US must be between 1 and 60s");
  static_assert(PIPE_BUFFER_SIZE >= 4096 && PIPE_BUFFER_SIZE <= 1024 * 1024,
                "PIPE_BUFFER_SIZE must be between 4KB and 1MB");
  static_assert(sizeof(hwnd_u64) >= 4, "hwnd_u64 must be at least 32 bits");

  // Version format check (compile-time)
  inline constexpr bool valid_version(std::string_view v)
  {
    if (v.empty())
      return false;
    size_t pos = (v[0] == 'v') ? 1 : 0;
    int parts = 0;
    while (pos < v.size()) {
      if (v[pos] < '0' || v[pos] > '9')
        return false;
      while (pos < v.size() && v[pos] >= '0' && v[pos] <= '9')
        pos++;
      parts++;
      if (pos < v.size() && v[pos] == '.')
        pos++;
    }
    return parts == 3;
  }
  static_assert(valid_version(PROTOCOL_VERSION), "PROTOCOL_VERSION must be N.N.N");
  static_assert(valid_version(WININSPECT_VERSION), "WININSPECT_VERSION must be vN.N.N");

  struct Rect
  {
    long left{}, top{}, right{}, bottom{};
  };

  struct Color
  {
    uint8_t r{}, g{}, b{};
    std::string to_hex() const;
  };

  struct ScreenCapture
  {
    int width{}, height{};
    std::string data_b64; // Base64 encoded BMP data
  };

  struct DesktopInfo
  {
    int width{}, height{};
    int dpi_x{}, dpi_y{};
    double scale_factor{};
  };

  struct ProcessInfo
  {
    uint32_t pid{};
    std::string name;
    std::string path;
  };

  struct ProcessExecResult
  {
    uint32_t pid{};
    std::string stdout_str;
    std::string stderr_str;
    int exit_code{};
  };

  struct FileInfo
  {
    std::string path;
    uint64_t size{};
    bool is_directory{};
    std::string last_modified;
  };

  struct RegistryValue
  {
    std::string name;
    std::string type; // "SZ", "DWORD", "BINARY", "MULTI_SZ"
    std::string data; // Hex string for binary, UTF-8 for strings
  };

  struct RegistryKeyInfo
  {
    std::string path;
    std::vector<std::string> subkeys;
    std::vector<RegistryValue> values;
  };

  struct ServiceInfo
  {
    std::string name;
    std::string display_name;
    std::string state; // "RUNNING", "STOPPED", etc.
  };

  struct DriveInfo
  {
    std::string letter;
    std::string mapping; // Target path (e.g., / or C:\)
    std::string type;    // "Fixed", "Remote", "CDROM", "RamDisk"
  };

  struct EnvVar
  {
    std::string name;
    std::string value;
  };

  struct MemoryRegion
  {
    uint64_t address{};
    std::string data_b64;
  };

  struct ImageMatchResult
  {
    int x{}, y{};
    double confidence{};
  };

  struct WindowInfo
  {
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

  struct WindowNode
  {
    hwnd_u64 hwnd{};
    std::string title;
    std::string class_name;
    std::vector<WindowNode> children;
  };

  struct PickFlags
  {
    bool prefer_child = true;
    bool ignore_transparent = true;
  };

  struct Snapshot
  {
    // Minimal snapshot for v1: stable list of top windows and their metadata.
    // Real implementations can expand this.
    std::vector<hwnd_u64> top;
  };

  struct Event
  {
    std::uint64_t seq{};
    std::string type; // "window.created", "window.destroyed", "window.changed"
    hwnd_u64 hwnd{};
    std::string property; // for "window.changed"
  };

  struct UIElementInfo
  {
    std::string automation_id;
    std::string name;
    std::string class_name;
    std::string control_type;
    Rect bounding_rect{};
    bool enabled = false;
    bool visible = false;
    std::vector<UIElementInfo> children;
  };

  // ── Control State ───────────────────────────────────────────────────

  enum class ControllerType : uint8_t { None = 0, Human = 1, Agent = 2, Script = 3 };

  inline const char* controller_type_str(ControllerType ct)
  {
    switch (ct) {
    case ControllerType::None:
      return "none";
    case ControllerType::Human:
      return "human";
    case ControllerType::Agent:
      return "agent";
    case ControllerType::Script:
      return "script";
    default:
      return "unknown";
    }
  }

  inline ControllerType controller_type_from_str(const std::string& s)
  {
    if (s == "human")
      return ControllerType::Human;
    if (s == "agent")
      return ControllerType::Agent;
    if (s == "script")
      return ControllerType::Script;
    return ControllerType::None;
  }

  struct AuditEntry
  {
    uint64_t seq{};
    int64_t timestamp{};
    std::string controller;
    std::string controller_id;
    std::string method;
    json::Object params;
    bool ok{};
    int64_t duration_ms{};
  };

  // ── Instance Identity ─────────────────────────────────────────────────

  struct InstanceIdentity
  {
    std::string uuid;        // RFC 4122 v4, auto-generated
    std::string name;        // user-supplied --instance-name or hostname
    std::string hostname;    // OS hostname
    std::string ecdh_pubkey; // base64 ECDH public key for mutual auth

    json::Object to_json() const;
    static InstanceIdentity from_json(const json::Object& o);
  };

  struct Capabilities
  {
    std::string os; // "windows 11", "windows 10", "windows (wine)"
    bool is_wine = false;
    std::string arch; // "x64", "x86"
    int win_major = 0;
    int win_minor = 0;
    int win_build = 0;

    // Feature flags (all runtime-detected)
    bool uia_available = false; // IUIAutomation COM interface
    bool clipboard_available = false;
    bool registry_writable = false;
    bool service_manager = false;  // SCManager access
    bool process_memory = false;   // ReadProcessMemory/WriteProcessMemory
    bool input_injection = false;  // SendInput
    bool window_highlight = false; // GDI drawing
    bool dxgi_capture = false;     // DXGI Desktop Duplication
    bool pipe_available = false;   // Named pipe transport (false under Wine)

    // Wine-specific
    std::string wine_version; // empty if not Wine
  };

  // Error code constants (used as strings in RPC responses)
  // E_BAD_SNAPSHOT — snapshot ID not found or invalid
  // E_SNAPSHOT_EVICTED — snapshot was valid but evicted by LRU
  inline constexpr const char* E_SNAPSHOT_EVICTED = "E_SNAPSHOT_EVICTED";

} // namespace wininspect
