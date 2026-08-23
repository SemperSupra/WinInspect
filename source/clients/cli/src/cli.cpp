#include "wininspect/base64.hpp"
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#ifdef _WIN32
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "wininspect/core.hpp"
#include "wininspect/tinyjson.hpp"
#include "wininspect/compress.hpp"
#include "wininspect/network_config.hpp"
#include "wininspect/mdns.hpp"
#include <set>

#include <filesystem>
#include <fstream>

#include "wininspect/crypto.hpp"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Advapi32.lib") // For CryptGenRandom

static std::wstring g_pipe_name = L"\\\\.\\pipe\\wininspectd";

struct Conn
{
  HANDLE hPipe = INVALID_HANDLE_VALUE;
  SOCKET s = INVALID_SOCKET;
  bool is_tcp = false;

  void close()
  {
    if (is_tcp) {
      if (s != INVALID_SOCKET) {
        closesocket(s);
        s = INVALID_SOCKET;
      }
    }
    else {
      if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
      }
    }
  }

  bool send(const std::string& m)
  {
    if (is_tcp) {
      uint32_t len = htonl((uint32_t)m.size());
      if (::send(s, (const char*)&len, 4, 0) <= 0)
        return false;
      if (::send(s, m.data(), (int)len, 0) <= 0)
        return false;
      return true;
    }
    else {
      DWORD written;
      uint32_t len = (uint32_t)m.size();
      if (!WriteFile(hPipe, &len, 4, &written, nullptr))
        return false;
      if (!WriteFile(hPipe, m.data(), (DWORD)len, &written, nullptr))
        return false;
      return true;
    }
  }

  bool recv(std::string& m)
  {
    if (is_tcp) {
      uint32_t len;
      int r = ::recv(s, (char*)&len, 4, 0);
      if (r <= 0)
        return false;
      len = ntohl(len);

      // Check compression flag (MSB set = deflate-compressed payload)
      bool is_compressed = false;
      if (len & wininspect::FRAME_COMPRESSED_FLAG) {
        is_compressed = true;
        len &= ~wininspect::FRAME_COMPRESSED_FLAG;
      }

      if (len == 0 || len > wininspect::MAX_MESSAGE_SIZE)
        return false;

      m.resize(len);
      r = ::recv(s, m.data(), (int)len, 0);
      if (r <= 0)
        return false;

      // Decompress if compression flag was set
      // After the length, the first 4 bytes of the compressed payload are the uncompressed size
      if (is_compressed) {
        if (len < 4)
          return false;
        uint32_t raw_size;
        memcpy(&raw_size, m.data(), 4);
        std::vector<uint8_t> compressed(m.begin() + 4, m.end());
        auto decompressed = wininspect::decompress(compressed, raw_size);
        if (decompressed.empty())
          return false;
        m.assign(decompressed.begin(), decompressed.end());
      }
      return true;
    }
    else {
      DWORD read;
      uint32_t len;
      if (!ReadFile(hPipe, &len, 4, &read, nullptr))
        return false;
      m.resize(len);
      if (!ReadFile(hPipe, m.data(), (DWORD)len, &read, nullptr))
        return false;
      return true;
    }
  }
};

static std::string get_config_path()
{
  // Primary: %APPDATA%\WinInspect\cli_config.json (Windows standard)
  // Fallback: ~/.wininspect_config (legacy — will migrate on write)
  std::string new_path = wininspect::default_config_dir() + "\\cli_config.json";
  if (std::ifstream(new_path).good())
    return new_path;
  // Legacy fallback
  const char* home = getenv("USERPROFILE");
  if (!home)
    home = getenv("HOME");
  if (home) {
    std::string legacy = std::string(home) + "/.wininspect_config";
    if (std::ifstream(legacy).good())
      return legacy;
  }
  return new_path; // will create on first write
}

static void save_key_path(const std::string& path)
{
  std::ofstream f(get_config_path());
  f << path;
}

static std::string load_key_path()
{
  std::ifstream f(get_config_path());
  std::string s;
  std::getline(f, s);
  return s;
}

static bool perform_auth(Conn& conn)
{
  std::string challenge_json;
  if (!conn.recv(challenge_json))
    return false;
  auto v = wininspect::json::parse(challenge_json);
  if (!v.is_obj() || v.as_obj().at("type").as_str() != "hello")
    return true; // No auth required (or old daemon)

  // Hello with no nonce means no auth required (daemon has no --auth-keys)
  auto obj = v.as_obj();
  if (obj.find("nonce") == obj.end())
    return true;

  std::string nonce_b64 = obj.at("nonce").as_str();
  std::string key_path = load_key_path();
  if (key_path.empty()) {
    std::cerr << "Daemon requires authentication. Set key with: wininspect "
                 "config --key <path>\n";
    return false;
  }

  std::string sig =
      wininspect::crypto::sign_ssh_msg(wininspect::base64::decode(nonce_b64), key_path);
  if (sig.empty()) {
    std::cerr << "Failed to sign challenge with key: " << key_path << "\n";
    return false;
  }

  wininspect::json::Object resp;
  resp["version"] = std::string(wininspect::PROTOCOL_VERSION);
  resp["identity"] = "wininspect-user";
  resp["signature"] = sig;
  if (!conn.send(wininspect::json::dumps(resp)))
    return false;

  std::string status_json;
  if (!conn.recv(status_json))
    return false;
  auto sv = wininspect::json::parse(status_json);
  return sv.is_obj() && sv.as_obj().at("type").as_str() == "auth_status" &&
         sv.as_obj().at("ok").as_bool();
}

static bool connect_daemon(Conn& conn, bool tcp, const std::string& host, int port)
{
  if (tcp) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
      return false;

    conn.s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (conn.s == INVALID_SOCKET)
      return false;

    u_long mode = 1;
    ioctlsocket(conn.s, FIONBIO, &mode);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    connect(conn.s, (sockaddr*)&addr, sizeof(addr));

    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(conn.s, &write_fds);
    timeval tv{2, 0};

    if (select(0, NULL, &write_fds, NULL, &tv) <= 0) {
      closesocket(conn.s);
      return false;
    }

    mode = 0;
    ioctlsocket(conn.s, FIONBIO, &mode);

    conn.is_tcp = true;
    if (!perform_auth(conn)) {
      conn.close();
      return false;
    }
    return true;
  }
  else {
    conn.hPipe = CreateFileW(g_pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (conn.hPipe == INVALID_HANDLE_VALUE)
      return false;
    conn.is_tcp = false;
    return true;
  }
}

static std::string make_req(const std::string& id, const std::string& method,
                            wininspect::json::Object params)
{
  using namespace wininspect::json;
  Object o;
  o["id"] = id;
  o["method"] = method;
  o["params"] = params;
  return dumps(o);
}

static int version()
{
  std::cout << "WinInspect " << wininspect::WININSPECT_VERSION
            << " — window inspection for Windows and Wine\n"
            << "Strix the Window Owl is your agent. You're always in control.\n"
            << "License: PolyForm Noncommercial 1.0.0 (free for non-commercial use)\n"
            << "  Commercial licenses: mark.e.deyoung+wininspect-license@gmail.com\n";
  return 0;
}

/// Check if --force is present in args. If not, print a warning and return false.
/// Used for destructive operations (kill, reg-delete, reg-write, exec).
static bool require_force(const std::vector<std::string>& args)
{
  for (auto& a : args) {
    if (a == "--force")
      return true;
  }
  std::cerr << "⚠ This is a destructive operation. Use --force to confirm.\n";
  return false;
}

static int usage()
{
  std::cerr
      << "WinInspect " << wininspect::WININSPECT_VERSION
      << " — window inspection for Windows and Wine\n"
      << "Strix the Window Owl is your agent. You're always in control.\n"
      << "\n"
      << "Usage: wininspect <command> [args] [--tcp host:port] [--pipe name]\n"
      << "\n"
      << "Commands:\n"
      << "  discover\n"
      << "  capture <left> <top> <right> <bottom>\n"
      << "  new-snap\n"
      << "  top [--snapshot s-..]\n"
      << "  info <hwnd> [--snapshot s-..]\n"
      << "  children <hwnd> [--snapshot s-..]\n"
      << "  tree [hwnd] [--snapshot s-..]\n"
      << "  pick <x> <y> [--snapshot s-..]\n"
      << "  highlight <hwnd>\n"
      << "  set-prop <hwnd> <name> <value>\n"
      << "  move <hwnd> <x> <y>\n"
      << "  resize <hwnd> <width> <height>\n"
      << "  z-order <hwnd>\n"
      << "  control-click <hwnd> <x> <y> [button]\n"
      << "  control-send <hwnd> <text>\n"
      << "  get-pixel <x> <y>\n"
      << "  drag <sx> <sy> <ex> <ey> [button] [duration_ms]\n"
      << "  pixel-search <left> <top> <right> <bottom> <r> <g> <b> [variation]\n"
      << "  desktop-info\n"
      << "  hotkey <keys>\n"
      << "  ps\n"
      << "  kill <pid> [--force]\n"
      << "  exec <command> [args] [--force]\n"
      << "  file-info <path>\n"
      << "  file-read <path>\n"
      << "  find-regex [title_regex] [class_regex]\n"
      << "  reg-read <path>\n"
      << "  reg-write <path> <name> <type> <data>\n"
      << "  reg-delete <path> [name]\n"
      << "  clip-read\n"
      << "  clip-write <text>\n"
      << "  svc-list\n"
      << "  svc-status <name>\n"
      << "  svc-control <name> <start|stop>\n"
      << "  env-get\n"
      << "  env-set <name> <value>\n"
      << "  wine-drives\n"
      << "  wine-overrides\n"
      << "  mutex-check <name>\n"
      << "  mutex-create <name> [own]\n"
      << "  mem-read <pid> <address> <size>\n"
      << "  mem-write <pid> <address> <base64_data>\n"
      << "  image-match <left> <top> <right> <bottom> <base64_bmp>\n"
      << "  input-hook <true|false>\n"
      << "  events-poll <new_snap_id> [old_snap_id] [--wait-ms ms]\n"
      << "  events-subscribe\n"
      << "  events-unsubscribe\n"
      << "  watch [--duration-sec <N>]\n"
      << "  status\n"
      << "  ensure-visible <hwnd> <true|false>\n"
      << "  ensure-foreground <hwnd>\n"
      << "  post-message <hwnd> <msg> [wparam] [lparam]\n"
      << "  send-input <base64_data>\n"
      << "  ui-inspect <hwnd>\n"
      << "  ui-invoke <hwnd> <automation_id>\n"
      << "  health\n"
      << "  identity\n"
      << "  capabilities\n"
      << "  daemon status\n"
      << "  daemon start\n"
      << "  daemon stop\n"
      << "  daemon restart\n"
      << "  check-update\n"
      << "  session record [--output <path>] [--interval <ms>] [--max-frames <n>]\n"
      << "  session replay <file> [--speed <n>]\n"
      << "  credential store --target <name> --username <user> [--file <path>] [--type <type>]\n"
      << "  credential retrieve --target <name> [--output-file <path>]\n"
      << "  credential delete --target <name>\n"
      << "  credential list\n"
      << "  credential generate --type password|ed25519 [--target <name>] [--length <n>]\n"
      << "  diag [--output <path>] [--include-logs] [--include-capture]\n"
      << "  update [--type portable|installer]\n"
      << "  metrics\n"
      << "  privacy-scan\n"
      << "  config --key <path>\n"
      << "  control\n"
      << "  control take [--controller human|agent|script] [--id <id>]\n"
      << "  control release\n"
      << "  control mode <auto|hybrid|human>\n"
      << "  control audit-log\n"
      << "\n"
      << "Configuration precedence (daemon flags):\n"
      << "  1. CLI flags (--port 1985)\n"
      << "  2. Environment variables (WININSPECT_PORT=1985)\n"
      << "  3. Config file (%APPDATA%\\WinInspect\\config.json)\n"
      << "  4. Registry (HKLM\\Software\\WinInspect\\...)\n"
      << "  5. Built-in defaults\n"
      << "  See docs/CONFIGURATION_MANAGEMENT.md for details\n"
      << "\n"
      << "Output format:\n"
      << "  --pretty   Human-readable output (tables, labels, colors)\n"
      << "  --json     Raw JSON output (default)\n";
  return 2;
}

// ── Pretty-print helpers ───────────────────────────────────────────────────

using namespace wininspect::json;

/// Format a single JSON value as a human-readable line.
static void pretty_print_value(std::ostream& os, const Value& v, int indent = 0)
{
  std::string pad(indent, ' ');
  if (v.is_str()) {
    os << pad << v.as_str();
  }
  else if (v.is_num()) {
    double d = v.as_num();
    if (d == (long long)d)
      os << pad << (long long)d;
    else
      os << pad << d;
  }
  else if (v.is_bool()) {
    os << pad << (v.as_bool() ? "yes" : "no");
  }
  else if (v.is_obj()) {
    auto& o = v.as_obj();
    bool first = true;
    for (auto& [k, val] : o) {
      if (!first) os << "\n";
      first = false;
      os << pad << k << ": ";
      pretty_print_value(os, val, indent + 2);
    }
  }
  else if (v.is_arr()) {
    auto& a = v.as_arr();
    for (size_t i = 0; i < a.size(); ++i) {
      if (i > 0) os << "\n";
      os << pad << "[" << (i + 1) << "] ";
      pretty_print_value(os, a[i], indent + 2);
    }
  }
  else {
    os << pad << "(null)";
  }
}

/// Pretty-print a daemon response. Override for specific methods.
static void pretty_print_result(std::ostream& os, const std::string& cmd,
                                 const Object& result)
{
  // ── Special-case formatters for common commands ──────────────────────────
  auto find = [&](const std::string& key) -> const Value* {
    auto it = result.find(key);
    return (it != result.end()) ? &it->second : nullptr;
  };

  if (cmd == "window.listTop" || cmd == "window.listChildren") {
    // Table of windows
    auto arr = find("windows");
    if (arr && arr->is_arr()) {
      for (auto& w : arr->as_arr()) {
        if (w.is_obj()) {
          auto& wo = w.as_obj();
          auto hwnd = wo.find("hwnd");
          auto title = wo.find("title");
          auto cls = wo.find("class");
          auto rect = wo.find("rect");
          os << (hwnd != wo.end() && hwnd->second.is_str() ? hwnd->second.as_str() : "?")
             << "  \"" << (title != wo.end() && title->second.is_str() ? title->second.as_str() : "")
             << "\"  [" << (cls != wo.end() && cls->second.is_str() ? cls->second.as_str() : "") << "]";
          if (rect != wo.end() && rect->second.is_obj()) {
            auto& r = rect->second.as_obj();
            auto x = r.find("x"), y = r.find("y"), w_ = r.find("width"), h = r.find("height");
            os << "  (" << (x != r.end() ? std::to_string((int)x->second.as_num()) : "?")
               << "," << (y != r.end() ? std::to_string((int)y->second.as_num()) : "?")
               << " " << (w_ != r.end() ? std::to_string((int)w_->second.as_num()) : "?")
               << "x" << (h != r.end() ? std::to_string((int)h->second.as_num()) : "?")
               << ")";
          }
          os << "\n";
        }
      }
    }
    else {
      os << "(no windows)\n";
    }
  }
  else if (cmd == "window.getInfo" || cmd == "window.getTree") {
    pretty_print_value(os, Value(Object(result)), 0);
    os << "\n";
  }
  else if (cmd == "window.findRegex") {
    auto arr = find("matches");
    if (arr && arr->is_arr()) {
      for (auto& m : arr->as_arr()) {
        if (m.is_obj()) {
          auto& mo = m.as_obj();
          auto hwnd = mo.find("hwnd");
          auto title = mo.find("title");
          os << (hwnd != mo.end() && hwnd->second.is_str() ? hwnd->second.as_str() : "?")
             << "  \"" << (title != mo.end() && title->second.is_str() ? title->second.as_str() : "") << "\"\n";
        }
      }
    }
    else {
      os << "(no matches)\n";
    }
  }
  else if (cmd == "daemon.identity") {
    auto uuid = find("uuid");
    auto name = find("name");
    auto hostname = find("hostname");
    auto version = find("version");
    auto deployment = find("deployment");
    auto license = find("license");
    auto os_info = find("os");
    auto arch = find("arch");
    if (name) os << "Name:       " << (name->is_str() ? name->as_str() : "?") << "\n";
    if (version) os << "Version:    " << (version->is_str() ? version->as_str() : "?") << "\n";
    if (uuid) os << "UUID:       " << (uuid->is_str() ? uuid->as_str() : "?") << "\n";
    if (hostname) os << "Hostname:   " << (hostname->is_str() ? hostname->as_str() : "?") << "\n";
    if (os_info) os << "OS:         " << (os_info->is_str() ? os_info->as_str() : "?") << "\n";
    if (arch) os << "Arch:       " << (arch->is_str() ? arch->as_str() : "?") << "\n";
    if (deployment) os << "Deployment: " << (deployment->is_str() ? deployment->as_str() : "?") << "\n";
    if (license) os << "License:    " << (license->is_str() ? license->as_str() : "?") << "\n";
  }
  else if (cmd == "daemon.status") {
    auto version = find("version");
    auto state = find("daemon_state");
    auto features = find("features");
    if (version) os << "Version: " << (version->is_str() ? version->as_str() : "?") << "\n";
    if (state) os << "State:   " << (state->is_str() ? state->as_str() : "?") << "\n";
    if (features && features->is_obj()) {
      os << "Features:\n";
      for (auto& [k, v] : features->as_obj()) {
        os << "  " << k << ": " << (v.is_bool() ? (v.as_bool() ? "available" : "unavailable") : "?") << "\n";
      }
    }
    auto deployment = find("deployment");
    auto license = find("license");
    if (deployment) os << "Deployment: " << (deployment->is_str() ? deployment->as_str() : "?") << "\n";
    if (license) os << "License:    " << (license->is_str() ? license->as_str() : "?") << "\n";
  }
  else if (cmd == "process.list") {
    auto arr = find("processes");
    if (arr && arr->is_arr()) {
      for (auto& p : arr->as_arr()) {
        if (p.is_obj()) {
          auto& po = p.as_obj();
          auto pid = po.find("pid");
          auto name = po.find("name");
          auto mem = po.find("memory_kb");
          if (pid != po.end()) os << (int)pid->second.as_num() << "  ";
          if (name != po.end()) os << (name->second.is_str() ? name->second.as_str() : "?");
          if (mem != po.end()) os << "  (" << (int)mem->second.as_num() << " KB)";
          os << "\n";
        }
      }
    }
    else {
      os << "(no processes)\n";
    }
  }
  else if (cmd == "control.status") {
    auto controller = find("current_controller");
    auto controller_id = find("controller_id");
    auto mode = find("mode");
    if (controller) os << "Controller: " << (controller->is_str() ? controller->as_str() : "?") << "\n";
    if (controller_id) os << "ID:         " << (controller_id->is_str() ? controller_id->as_str() : "?") << "\n";
    if (mode) os << "Mode:       " << (mode->is_str() ? mode->as_str() : "?") << "\n";
  }
  else if (cmd == "daemon.capabilities") {
    for (auto& [k, v] : result) {
      if (v.is_bool()) {
        os << k << ": " << (v.as_bool() ? "yes" : "no") << "\n";
      }
      else if (v.is_str()) {
        os << k << ": " << v.as_str() << "\n";
      }
      else if (v.is_num()) {
        os << k << ": " << (int)v.as_num() << "\n";
      }
      else {
        os << k << ": ";
        pretty_print_value(os, v, 0);
        os << "\n";
      }
    }
  }
  else if (cmd == "screen.getPixel") {
    auto r = find("r"), g = find("g"), b = find("b"), hex = find("hex");
    if (r && g && b)
      os << "RGB: (" << (int)r->as_num() << ", " << (int)g->as_num() << ", "
         << (int)b->as_num() << ")\n";
    if (hex) os << "Hex: " << (hex->is_str() ? hex->as_str() : "?") << "\n";
  }
  else if (cmd == "daemon.metrics") {
    for (auto& [k, v] : result) {
      os << k << ":\n";
      if (v.is_obj()) {
        for (auto& [k2, v2] : v.as_obj()) {
          os << "  " << k2 << ": ";
          pretty_print_value(os, v2, 0);
          os << "\n";
        }
      }
      else {
        os << "  ";
        pretty_print_value(os, v, 0);
        os << "\n";
      }
    }
  }
  else if (cmd == "env.get") {
    for (auto& [k, v] : result) {
      os << k << "=" << (v.is_str() ? v.as_str() : "?") << "\n";
    }
  }
  else if (cmd == "wine.drives") {
    for (auto& [k, v] : result) {
      os << k << ": " << (v.is_str() ? v.as_str() : "?") << "\n";
    }
  }
  else if (cmd == "wine.overrides") {
    for (auto& [k, v] : result) {
      os << k << ": " << (v.is_str() ? v.as_str() : "?") << "\n";
    }
  }
  else if (cmd == "clipboard.read") {
    auto text = find("text");
    if (text && text->is_str())
      os << text->as_str() << "\n";
  }
  else if (cmd == "image.match") {
    auto matches = find("matches");
    auto count = find("count");
    if (count) os << "Matches: " << (int)count->as_num() << "\n";
    if (matches && matches->is_arr()) {
      for (auto& m : matches->as_arr()) {
        if (m.is_obj()) {
          auto& mo = m.as_obj();
          os << "  ";
          for (auto& [k, v] : mo) {
            if (v.is_num()) os << k << "=" << (int)v.as_num() << " ";
            else if (v.is_str()) os << k << "=" << v.as_str() << " ";
            else if (v.is_bool()) os << k << "=" << (v.as_bool() ? "yes" : "no") << " ";
          }
          os << "\n";
        }
      }
    }
  }
  else if (cmd == "events.poll") {
    auto events = find("events");
    if (events && events->is_arr()) {
      for (auto& evt : events->as_arr()) {
        if (evt.is_obj()) {
          auto& eo = evt.as_obj();
          auto type = eo.find("type");
          auto source = eo.find("source_hwnd");
          auto ts = eo.find("timestamp");
          if (ts != eo.end()) os << "[" << (int)ts->second.as_num() << "ms] ";
          if (type != eo.end()) os << (type->second.is_str() ? type->second.as_str() : "?");
          if (source != eo.end()) os << " hwnd=" << (source->second.is_str() ? source->second.as_str() : "?");
          os << "\n";
        }
      }
    }
  }
  else if (cmd == "service.list") {
    auto arr = find("services");
    if (arr && arr->is_arr()) {
      for (auto& svc : arr->as_arr()) {
        if (svc.is_obj()) {
          auto& so = svc.as_obj();
          auto name = so.find("name");
          auto display = so.find("display_name");
          auto state = so.find("state");
          if (name != so.end()) os << (name->second.is_str() ? name->second.as_str() : "?");
          if (display != so.end()) os << "  \"" << (display->second.is_str() ? display->second.as_str() : "") << "\"";
          if (state != so.end()) os << "  [" << (state->second.is_str() ? state->second.as_str() : "?") << "]";
          os << "\n";
        }
      }
    }
  }
  else if (cmd == "screen.desktopInfo") {
    auto w = find("width"), h = find("height"), dpi = find("dpi"),
         bpp = find("bits_per_pixel"), monitors = find("monitor_count");
    if (w && h) os << "Resolution: " << (int)w->as_num() << "x" << (int)h->as_num() << "\n";
    if (dpi) os << "DPI:       " << (int)dpi->as_num() << "\n";
    if (bpp) os << "BPP:       " << (int)bpp->as_num() << "\n";
    if (monitors) os << "Monitors:  " << (int)monitors->as_num() << "\n";
  }
  else if (cmd == "reg.read") {
    auto value = find("value");
    auto type = find("type");
    if (value && value->is_str()) os << "Value: " << value->as_str() << "\n";
    else if (value) { os << "Value: "; pretty_print_value(os, *value, 0); os << "\n"; }
    if (type) os << "Type:  " << (type->is_str() ? type->as_str() : "?") << "\n";
  }
  else if (cmd == "file.getInfo") {
    auto size = find("size");
    auto modified = find("modified");
    auto is_dir = find("is_directory");
    if (size) os << "Size:    " << (long long)size->as_num() << " bytes\n";
    if (modified) os << "Modified: " << (modified->is_str() ? modified->as_str() : "?") << "\n";
    if (is_dir) os << "Type:    " << (is_dir->as_bool() ? "directory" : "file") << "\n";
  }
  else if (cmd == "screen.pixelSearch") {
    auto found = find("found");
    auto x = find("x"), y = find("y");
    auto count = find("count");
    if (found && found->is_bool()) {
      os << (found->as_bool() ? "Found" : "Not found");
      if (found->as_bool() && x && y)
        os << " at (" << (int)x->as_num() << ", " << (int)y->as_num() << ")";
      if (count) os << " (" << (int)count->as_num() << " matches)";
      os << "\n";
    }
  }
  else if (cmd == "window.highlight") {
    auto ok = find("ok");
    os << (ok && ok->is_bool() && ok->as_bool() ? "Highlighted" : "Failed") << "\n";
  }
  else if (cmd == "control.auditLog") {
    auto entries = find("entries");
    if (entries && entries->is_arr()) {
      for (auto& e : entries->as_arr()) {
        if (e.is_obj()) {
          auto& eo = e.as_obj();
          auto ts = eo.find("timestamp");
          auto ctrl = eo.find("controller");
          auto method = eo.find("method");
          auto ok = eo.find("ok");
          if (ts != eo.end()) os << "[" << (ts->second.is_str() ? ts->second.as_str() : "?") << "] ";
          if (ctrl != eo.end()) os << (ctrl->second.is_str() ? ctrl->second.as_str() : "?") << " ";
          if (method != eo.end()) os << (method->second.is_str() ? method->second.as_str() : "?");
          if (ok != eo.end()) os << " " << (ok->second.as_bool() ? "OK" : "FAIL");
          os << "\n";
        }
      }
    }
  }
  else {
    // Generic fallback: key: value
    pretty_print_value(os, Value(Object(result)), 0);
    os << "\n";
  }
}

int main(int argc, char** argv)
{
  if (argc < 2)
    return usage();

  bool use_tcp = false;
  std::string tcp_host = "127.0.0.1";
  int tcp_port = 1985;
  std::string session_id_arg;
  bool g_pretty = false;

  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--pipe" && i + 1 < argc) {
      std::string pname = argv[++i];
      g_pipe_name = L"\\\\.\\pipe\\" + std::wstring(pname.begin(), pname.end());
    }
    else if (std::string(argv[i]) == "--tcp") {
      use_tcp = true;
      // Only consume next arg if it looks like a host:port or IP address
      if (i + 1 < argc) {
        std::string next = argv[i + 1];
        // Peek: starts with digit, colon, or bracket (IPv6) → it's an address
        if (!next.empty() && (std::isdigit((unsigned char)next[0]) || next[0] == '[' ||
                              next.find(':') != std::string::npos)) {
          std::string host_port = argv[++i];
          size_t colon = host_port.find(':');
          if (colon != std::string::npos) {
            tcp_host = host_port.substr(0, colon);
            tcp_port = std::stoi(host_port.substr(colon + 1));
          }
          else {
            tcp_host = host_port;
          }
        }
      }
    }
    else if (std::string(argv[i]) == "--session-id" && i + 1 < argc) {
      session_id_arg = argv[i + 1];
      i++;
    }
    else if (std::string(argv[i]) == "--pretty") {
      g_pretty = true;
    }
    else if (std::string(argv[i]) == "--json") {
      g_pretty = false;
    }
    else if (std::string(argv[i]) == "--version" || std::string(argv[i]) == "-v") {
      return version();
    }
    else {
      args.push_back(argv[i]);
    }
  }

  if (args.empty())
    return usage();
  std::string cmd = args[0];

  using namespace wininspect::json;
  Object params;
  params["canonical"] = true;
  // Protocol version — server rejects mismatched versions with clear error
  params["protocol_version"] = std::string(wininspect::PROTOCOL_VERSION);
  // Signal compression support — server will compress large responses
  params["accept_encoding"] = std::string("zlib");
  if (!session_id_arg.empty()) {
    params["session_id"] = session_id_arg;
  }

  auto get_snapshot = [&](size_t& i) {
    if (i + 1 < args.size() && args[i] == "--snapshot") {
      params["snapshot_id"] = args[i + 1];
      i += 2;
      return true;
    }
    return false;
  };

  auto send_and_print = [&](const std::string& method) -> int {
    Conn conn;
    if (!connect_daemon(conn, use_tcp, tcp_host, tcp_port)) {
      std::cerr << "failed to connect to daemon\n";
      return 1;
    }
    // connect_daemon already consumed the daemon's hello challenge
    // (TCP path calls perform_auth; named pipe is auto-authenticated).
    std::string req = make_req("cli-1", method, params);
    std::string resp;

    // Show progress indicator for potentially slow operations
    bool known_slow = (method == "process.execute" || method == "daemon.downloadUpdate" ||
                       method.find("mem.read") != std::string::npos);
    if (known_slow) {
      std::cerr << "⏳ waiting for " << method << " ...\n";
    }

    if (!conn.send(req) || !conn.recv(resp)) {
      std::cerr << "communication error\n";
      conn.close();
      return 1;
    }
    if (g_pretty) {
      try {
        auto v = parse(resp);
        if (v.is_obj()) {
          auto& obj = v.as_obj();
          auto result_it = obj.find("result");
          auto ok_it = obj.find("ok");
          bool ok = (ok_it != obj.end() && ok_it->second.is_bool() && ok_it->second.as_bool());
          if (!ok) {
            auto err = obj.find("error_message");
            std::cout << "Error: " << (err != obj.end() && err->second.is_str() ? err->second.as_str() : "unknown") << "\n";
          }
          else if (result_it != obj.end() && result_it->second.is_obj()) {
            pretty_print_result(std::cout, method, result_it->second.as_obj());
          }
          else {
            std::cout << "ok\n";
          }
        }
        else {
          std::cout << resp << "\n";
        }
      }
      catch (...) {
        std::cout << resp << "\n";
      }
    }
    else {
      std::cout << resp << "\n";
    }
    conn.close();
    return 0;
  };

  if (cmd == "discover") {
    int disc_port = 1986;
    int disc_timeout_ms = 2000;

    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--discovery-port" && i + 1 < argc) {
        disc_port = std::stoi(argv[++i]);
      }
      if (std::string(argv[i]) == "--discovery-timeout" && i + 1 < argc) {
        disc_timeout_ms = std::stoi(argv[++i]);
      }
    }

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bool broadcast = true;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)disc_port);
    addr.sin_addr.s_addr = INADDR_BROADCAST;

    std::string msg = "WININSPECT_DISCOVER";
    sendto(s, msg.data(), (int)msg.size(), 0, (struct sockaddr*)&addr, sizeof(addr));

    // Also try loopback directly as broadcast is often blocked/restricted in containers
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    sendto(s, msg.data(), (int)msg.size(), 0, (struct sockaddr*)&addr, sizeof(addr));

    std::cout << "Scanning for WinInspect daemons on port " << disc_port << "...\n";
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv{disc_timeout_ms / 1000, (disc_timeout_ms % 1000) * 1000};

    while (select(0, &fds, NULL, NULL, &tv) > 0) {
      char buf[4096];
      struct sockaddr_in from;
      int from_len = sizeof(from);
      int r = recvfrom(s, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &from_len);
      if (r > 0) {
        buf[r] = '\0';
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip, INET_ADDRSTRLEN);
        std::cout << "[" << ip << "] " << buf << "\n";
      }
      FD_ZERO(&fds);
      FD_SET(s, &fds);
      tv = {0, 500000}; // quick check for more
    }
    closesocket(s);

    // Also query mDNS for _wininspect._tcp.local services
    SOCKET mdns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (mdns_sock != INVALID_SOCKET) {
      int reuse = 1;
      setsockopt(mdns_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
      DWORD mcast_ttl = 255;
      setsockopt(mdns_sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&mcast_ttl,
                 sizeof(mcast_ttl));
      DWORD rcv_timeout = disc_timeout_ms;
      setsockopt(mdns_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcv_timeout,
                 sizeof(rcv_timeout));

      struct sockaddr_in mcast_addr;
      memset(&mcast_addr, 0, sizeof(mcast_addr));
      mcast_addr.sin_family = AF_INET;
      mcast_addr.sin_port = htons(5353);
      mcast_addr.sin_addr.s_addr = inet_addr("224.0.0.251");

      // Build mDNS PTR query for _wininspect._tcp.local
      std::vector<uint8_t> mdns_query(12, 0);
      mdns_query[5] = 0x01; // QDCOUNT high byte
      auto add_label = [&](const std::string& label) {
        mdns_query.push_back((uint8_t)label.size());
        mdns_query.insert(mdns_query.end(), label.begin(), label.end());
      };
      add_label("_wininspect");
      add_label("_tcp");
      add_label("local");
      mdns_query.push_back(0);
      uint8_t qtype_class[4] = {0, 12, 0, 1};
      mdns_query.insert(mdns_query.end(), qtype_class, qtype_class + 4);

      sendto(mdns_sock, (const char*)mdns_query.data(), (int)mdns_query.size(), 0,
             (struct sockaddr*)&mcast_addr, sizeof(mcast_addr));
      std::cout << "Querying mDNS for _wininspect._tcp.local...\n";

      uint8_t mdns_buf[1500];
      while (true) {
        struct sockaddr_in from;
        int from_len = sizeof(from);
        int r = recvfrom(mdns_sock, (char*)mdns_buf, sizeof(mdns_buf), 0, (struct sockaddr*)&from,
                         &from_len);
        if (r <= 0)
          break;
        if (r < 12 || (mdns_buf[2] & 0x80) == 0)
          continue;
        std::string payload((char*)mdns_buf, r);
        if (payload.find("wininspect") != std::string::npos) {
          char ip[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &from.sin_addr, ip, INET_ADDRSTRLEN);
          std::cout << "[" << ip << ":5353] (mDNS) _wininspect._tcp.local\n";
        }
      }
      closesocket(mdns_sock);
    }

    // Use MdnsResponder::discover() for structured results
    auto mdns_results = wininspect::MdnsResponder::discover("_wininspect", disc_timeout_ms / 1000);
    for (auto& [host, port] : mdns_results) {
      std::cout << "[mDNS] " << host << ":" << port << std::endl;
    }

    // --watch mode
    bool watch_mode = false;
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--watch")
        watch_mode = true;
    }
    if (watch_mode) {
      std::cout << "Watching for daemons (Ctrl+C to stop)..." << std::endl;
      std::set<std::string> seen;
      for (auto& [h, p] : mdns_results)
        seen.insert(h + ":" + std::to_string(p));
      while (true) {
        Sleep(5000);
        auto nr = wininspect::MdnsResponder::discover("_wininspect", 2);
        for (auto& [host, port] : nr) {
          std::string key = host + ":" + std::to_string(port);
          if (seen.find(key) == seen.end()) {
            seen.insert(key);
            std::cout << "[NEW] " << host << ":" << port << std::endl;
          }
        }
      }
    }

    WSACleanup();
    return 0;
  }

  if (cmd == "new-snap") {
    return send_and_print("snapshot.capture");
  }

  if (cmd == "capture") {
    if (args.size() < 5)
      return usage();
    params["left"] = std::stod(args[1]);
    params["top"] = std::stod(args[2]);
    params["right"] = std::stod(args[3]);
    params["bottom"] = std::stod(args[4]);
    return send_and_print("screen.capture");
  }

  if (cmd == "top") {
    for (size_t i = 1; i < args.size();)
      if (!get_snapshot(i))
        i++;
    return send_and_print("window.listTop");
  }

  if (cmd == "info") {
    if (args.size() < 2)
      return usage();
    params["hwnd"] = args[1];
    for (size_t i = 2; i < args.size();)
      if (!get_snapshot(i))
        i++;
    return send_and_print("window.getInfo");
  }

  if (cmd == "children") {
    if (args.size() < 2)
      return usage();
    params["hwnd"] = args[1];
    for (size_t i = 2; i < args.size();)
      if (!get_snapshot(i))
        i++;
    return send_and_print("window.listChildren");
  }

  if (cmd == "tree") {
    if (args.size() >= 2 && args[1].rfind("0x", 0) == 0) {
      params["hwnd"] = args[1];
      for (size_t i = 2; i < args.size();)
        if (!get_snapshot(i))
          i++;
    }
    else {
      for (size_t i = 1; i < args.size();)
        if (!get_snapshot(i))
          i++;
    }
    return send_and_print("window.getTree");
  }

  if (cmd == "highlight") {
    if (args.size() < 2)
      return usage();
    params["hwnd"] = args[1];
    return send_and_print("window.highlight");
  }

  if (cmd == "pick") {
    if (args.size() < 3)
      return usage();
    params["x"] = std::stod(args[1]);
    params["y"] = std::stod(args[2]);
    for (size_t i = 3; i < args.size();)
      if (!get_snapshot(i))
        i++;
    return send_and_print("window.pickAtPoint");
  }

  if (cmd == "set-prop") {
    if (args.size() < 4)
      return usage();
    params["hwnd"] = args[1];
    params["name"] = args[2];
    params["value"] = args[3];
    return send_and_print("window.setProperty");
  }

  if (cmd == "move") {
    if (args.size() < 4)
      return usage();
    params["hwnd"] = args[1];
    params["x"] = std::stod(args[2]);
    params["y"] = std::stod(args[3]);
    return send_and_print("window.move");
  }

  if (cmd == "resize") {
    if (args.size() < 4)
      return usage();
    params["hwnd"] = args[1];
    params["width"] = std::stod(args[2]);
    params["height"] = std::stod(args[3]);
    return send_and_print("window.resize");
  }

  if (cmd == "z-order") {
    if (args.size() < 2)
      return usage();
    params["hwnd"] = args[1];
    return send_and_print("window.getZOrder");
  }

  if (cmd == "control-click") {
    if (args.size() < 4)
      return usage();
    params["hwnd"] = args[1];
    params["x"] = std::stod(args[2]);
    params["y"] = std::stod(args[3]);
    if (args.size() > 4)
      params["button"] = std::stod(args[4]);
    return send_and_print("window.controlClick");
  }

  if (cmd == "control-send") {
    if (args.size() < 3)
      return usage();
    params["hwnd"] = args[1];
    params["text"] = args[2];
    return send_and_print("window.controlSend");
  }

  if (cmd == "get-pixel") {
    if (args.size() < 3)
      return usage();
    params["x"] = std::stod(args[1]);
    params["y"] = std::stod(args[2]);
    return send_and_print("screen.getPixel");
  }

  if (cmd == "pixel-search") {
    if (args.size() < 8)
      return usage();
    params["left"] = std::stod(args[1]);
    params["top"] = std::stod(args[2]);
    params["right"] = std::stod(args[3]);
    params["bottom"] = std::stod(args[4]);
    params["r"] = std::stod(args[5]);
    params["g"] = std::stod(args[6]);
    params["b"] = std::stod(args[7]);
    if (args.size() > 8)
      params["variation"] = std::stod(args[8]);
    return send_and_print("screen.pixelSearch");
  }

  if (cmd == "drag") {
    if (args.size() < 5)
      return usage();
    params["start_x"] = std::stod(args[1]);
    params["start_y"] = std::stod(args[2]);
    params["end_x"] = std::stod(args[3]);
    params["end_y"] = std::stod(args[4]);
    if (args.size() > 5)
      params["button"] = std::stod(args[5]);
    if (args.size() > 6)
      params["duration_ms"] = std::stod(args[6]);
    return send_and_print("input.mouseDrag");
  }

  if (cmd == "desktop-info") {
    return send_and_print("screen.desktopInfo");
  }

  if (cmd == "hotkey") {
    if (args.size() < 2)
      return usage();
    params["keys"] = args[1];
    return send_and_print("input.hotkey");
  }

  if (cmd == "ps") {
    return send_and_print("process.list");
  }

  if (cmd == "kill") {
    if (args.size() < 2)
      return usage();
    if (!require_force(args))
      return 1;
    params["pid"] = std::stod(args[1]);
    return send_and_print("process.kill");
  }

  if (cmd == "exec") {
    if (args.size() < 2)
      return usage();
    if (!require_force(args))
      return 1;
    params["command"] = args[1];
    if (args.size() > 2) {
      std::string all_args;
      for (size_t i = 2; i < args.size(); i++) {
        if (i > 2)
          all_args += " ";
        all_args += args[i];
      }
      params["args"] = all_args;
    }
    return send_and_print("process.execute");
  }

  if (cmd == "file-info") {
    if (args.size() < 2)
      return usage();
    params["path"] = args[1];
    return send_and_print("file.getInfo");
  }

  if (cmd == "file-read") {
    if (args.size() < 2)
      return usage();
    params["path"] = args[1];
    return send_and_print("file.read");
  }

  if (cmd == "find-regex") {
    if (args.size() > 1)
      params["title_regex"] = args[1];
    if (args.size() > 2)
      params["class_regex"] = args[2];
    return send_and_print("window.findRegex");
  }

  if (cmd == "reg-read") {
    if (args.size() < 2)
      return usage();
    params["path"] = args[1];
    return send_and_print("reg.read");
  }

  if (cmd == "reg-write") {
    if (args.size() < 5)
      return usage();
    if (!require_force(args))
      return 1;
    params["path"] = args[1];
    params["name"] = args[2];
    params["type"] = args[3];
    params["data"] = args[4];
    return send_and_print("reg.write");
  }

  if (cmd == "reg-delete") {
    if (args.size() < 2)
      return usage();
    if (!require_force(args))
      return 1;
    params["path"] = args[1];
    if (args.size() > 2)
      params["name"] = args[2];
    return send_and_print("reg.delete");
  }

  if (cmd == "clip-read")
    return send_and_print("clipboard.read");

  if (cmd == "clip-write") {
    if (args.size() < 2)
      return usage();
    params["text"] = args[1];
    return send_and_print("clipboard.write");
  }

  if (cmd == "svc-list")
    return send_and_print("service.list");

  if (cmd == "svc-status") {
    if (args.size() < 2)
      return usage();
    params["name"] = args[1];
    return send_and_print("service.status");
  }

  if (cmd == "svc-control") {
    if (args.size() < 3)
      return usage();
    params["name"] = args[1];
    params["action"] = args[2];
    return send_and_print("service.control");
  }

  if (cmd == "env-get")
    return send_and_print("env.get");

  if (cmd == "env-set") {
    if (args.size() < 3)
      return usage();
    params["name"] = args[1];
    params["value"] = args[2];
    return send_and_print("env.set");
  }

  if (cmd == "wine-drives")
    return send_and_print("wine.drives");
  if (cmd == "wine-overrides")
    return send_and_print("wine.overrides");

  if (cmd == "mutex-check") {
    if (args.size() < 2)
      return usage();
    params["name"] = args[1];
    return send_and_print("sync.checkMutex");
  }

  if (cmd == "mutex-create") {
    if (args.size() < 2)
      return usage();
    params["name"] = args[1];
    if (args.size() > 2)
      params["own"] = (args[2] == "true");
    return send_and_print("sync.createMutex");
  }

  if (cmd == "mem-read") {
    if (args.size() < 4)
      return usage();
    params["pid"] = std::stod(args[1]);
    params["address"] = (double)std::stoull(args[2], nullptr, 0);
    params["size"] = std::stod(args[3]);
    return send_and_print("mem.read");
  }

  if (cmd == "mem-write") {
    if (args.size() < 4)
      return usage();
    params["pid"] = std::stod(args[1]);
    params["address"] = (double)std::stoull(args[2], nullptr, 0);
    params["data_b64"] = args[3];
    return send_and_print("mem.write");
  }

  if (cmd == "image-match") {
    if (args.size() < 6)
      return usage();
    params["left"] = std::stod(args[1]);
    params["top"] = std::stod(args[2]);
    params["right"] = std::stod(args[3]);
    params["bottom"] = std::stod(args[4]);
    params["sub_image_b64"] = args[5];
    return send_and_print("image.match");
  }

  if (cmd == "input-hook") {
    if (args.size() < 2)
      return usage();
    params["enabled"] = (args[1] == "true");
    return send_and_print("input.hook");
  }

  if (cmd == "events-poll") {
    if (args.size() < 2)
      return usage();
    params["snapshot_id"] = args[1];
    if (args.size() > 2 && args[2].rfind("0x", 0) != 0 && args[2].find("--") == std::string::npos)
      params["old_snapshot_id"] = args[2];

    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--wait-ms" && i + 1 < argc) {
        params["wait_ms"] = std::stod(argv[i + 1]);
      }
    }
    return send_and_print("events.poll");
  }

  if (cmd == "events-subscribe") {
    return send_and_print("events.subscribe");
  }

  if (cmd == "events-unsubscribe") {
    return send_and_print("events.unsubscribe");
  }

  if (cmd == "watch") {
    // Parse --duration-sec flag
    int duration_sec = 300; // default 5 min, 0 = infinite
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--duration-sec" && i + 1 < argc)
        duration_sec = std::stoi(argv[++i]);
    }

    Conn conn;
    if (!connect_daemon(conn, use_tcp, tcp_host, tcp_port))
      return 1;

    std::string resp;
    std::cout << "Watching for window events..."
              << (duration_sec > 0 ? " (" + std::to_string(duration_sec) + "s)" : " (infinite)")
              << " (Ctrl+C to stop)\n";

    // Start with a fresh snapshot
    Object snap_params;
    snap_params["canonical"] = true;
    std::string snap_req = make_req("w-0", "snapshot.capture", snap_params);
    if (!conn.send(snap_req)) {
      conn.close();
      return 1;
    }
    if (!conn.recv(resp)) {
      conn.close();
      return 1;
    }

    // Parse snapshot ID from response
    std::string sid;
    try {
      auto v = wininspect::json::parse(resp);
      if (v.is_obj()) {
        auto o = v.as_obj();
        auto it_r = o.find("result");
        if (it_r != o.end() && it_r->second.is_obj()) {
          auto r = it_r->second.as_obj();
          auto it_s = r.find("snapshot_id");
          if (it_s != r.end() && it_s->second.is_str())
            sid = it_s->second.as_str();
        }
      }
    }
    catch (...) {
    }
    if (sid.empty()) {
      std::cerr << "Failed to capture initial snapshot\n";
      conn.close();
      return 1;
    }

    DWORD poll_interval_ms = 100;
    DWORD min_interval = 100;
    DWORD max_interval = 5000;
    auto start_time = GetTickCount();
    bool connected = true;

    while (true) {
      // Check duration expiry
      if (duration_sec > 0) {
        DWORD elapsed = (GetTickCount() - start_time) / 1000;
        if ((int)elapsed >= duration_sec)
          break;
      }

      Sleep(poll_interval_ms);

      if (!connected) {
        // Try to reconnect with exponential backoff
        static int reconnect_attempt = 0;
        conn.close();
        Sleep(std::min(5000u, 100u << reconnect_attempt)); // 100ms, 200ms, 400ms,... 5s max
        if (connect_daemon(conn, use_tcp, tcp_host, tcp_port)) {
          reconnect_attempt = 0;
          std::cerr << "\n[Reconnected]\n";
          connected = true;
          continue;
        }
        reconnect_attempt++;
        continue; // keep trying until duration expires or Ctrl+C
      }

      // Build poll request with proper snapshot_id
      Object poll_params;
      poll_params["canonical"] = true;
      poll_params["snapshot_id"] = sid;
      poll_params["wait_ms"] = 0.0; // return immediately, no events is not an error
      std::string poll_req = make_req("w-1", "events.poll", poll_params);

      if (!conn.send(poll_req)) {
        std::cerr << "\n[Connection lost, reconnecting...]\n";
        connected = false;
        continue;
      }

      resp.clear();
      if (!conn.recv(resp)) {
        std::cerr << "\n[Connection lost, reconnecting...]\n";
        connected = false;
        continue;
      }

      // Print non-empty poll results and adapt polling interval
      bool had_events = false;
      try {
        auto v = parse(resp);
        if (v.is_obj()) {
          auto o = v.as_obj();
          auto it_r = o.find("result");
          if (it_r != o.end() && it_r->second.is_arr()) {
            auto arr = it_r->second.as_arr();
            if (!arr.empty()) {
              had_events = true;
              for (auto& evt : arr) {
                if (evt.is_obj())
                  std::cout << dumps(evt) << "\n";
              }
              std::cout.flush();
            }
          }
        }
      }
      catch (...) {
      }

      // Adaptive polling: reset to min interval on events, back off on empty
      if (had_events) {
        poll_interval_ms = min_interval;
      }
      else {
        poll_interval_ms =
            poll_interval_ms * 2 < max_interval ? poll_interval_ms * 2 : max_interval;
      }
    }
    conn.close();
    return 0;
  }

  if (cmd == "status") {
    return send_and_print("daemon.status");
  }

  if (cmd == "ensure-visible") {
    if (args.size() < 3)
      return usage();
    params["hwnd"] = args[1];
    params["visible"] = (args[2] == "true");
    return send_and_print("window.ensureVisible");
  }

  if (cmd == "ensure-foreground") {
    if (args.size() < 2)
      return usage();
    params["hwnd"] = args[1];
    return send_and_print("window.ensureForeground");
  }

  if (cmd == "post-message") {
    if (args.size() < 3)
      return usage();
    params["hwnd"] = args[1];
    params["msg"] = std::stod(args[2]);
    if (args.size() > 3)
      params["wparam"] = std::stod(args[3]);
    if (args.size() > 4)
      params["lparam"] = std::stod(args[4]);
    return send_and_print("window.postMessage");
  }

  if (cmd == "send-input") {
    if (args.size() < 2)
      return usage();
    params["data_b64"] = args[1];
    return send_and_print("input.send");
  }

  if (cmd == "ui-inspect") {
    if (args.size() < 2)
      return usage();
    params["hwnd"] = args[1];
    return send_and_print("ui.inspect");
  }

  if (cmd == "ui-invoke") {
    if (args.size() < 3)
      return usage();
    params["hwnd"] = args[1];
    params["automation_id"] = args[2];
    return send_and_print("ui.invoke");
  }

  if (cmd == "health") {
    return send_and_print("daemon.health");
  }

  if (cmd == "identity") {
    return send_and_print("daemon.identity");
  }

  if (cmd == "capabilities") {
    return send_and_print("daemon.capabilities");
  }

  // ── Daemon lifecycle ─────────────────────────────────────────────────
  if (cmd == "daemon") {
    if (args.size() < 2)
      return usage();
    std::string sub = args[1];

    if (sub == "status") {
      Conn conn;
      if (connect_daemon(conn, use_tcp, tcp_host, tcp_port)) {
        conn.close();
        // Connected — get detailed status
        auto resp = send_and_print("daemon.status");
        std::cout << "Daemon: running\n";
        return resp;
      }
      std::cout << "Daemon: stopped\n";
      return 0;
    }

    if (sub == "start") {
      // Check if already running
      Conn conn;
      if (connect_daemon(conn, use_tcp, tcp_host, tcp_port)) {
        conn.close();
        std::cout << "Daemon is already running.\n";
        return 0;
      }
      // Find wininspectd.exe next to the CLI binary
      wchar_t modulePath[MAX_PATH];
      GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
      std::wstring dir = modulePath;
      auto pos = dir.find_last_of(L"\\");
      if (pos != std::wstring::npos)
        dir = dir.substr(0, pos + 1);
      std::wstring daemonPath = dir + L"wininspectd.exe";
      // Use headless mode since we're launching from CLI
      std::wstring cmdLine = L"\"" + daemonPath + L"\" --headless";
      STARTUPINFOW si = {sizeof(si)};
      PROCESS_INFORMATION pi;
      if (CreateProcessW(daemonPath.c_str(), &cmdLine[0], nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        std::cout << "Daemon started.\n";
        // Wait briefly for daemon to initialize, then verify
        for (int retry = 0; retry < 10; retry++) {
          Sleep(500);
          Conn c2;
          if (connect_daemon(c2, use_tcp, tcp_host, tcp_port)) {
            c2.close();
            std::cout << "Daemon ready.\n";
            return 0;
          }
        }
        std::cout << "Warning: Daemon launched but not responding yet.\n";
        return 0;
      }
      std::cerr << "Failed to start daemon.\n";
      return 1;
    }

    if (sub == "stop") {
      // Connect to daemon and terminate it gracefully
      Conn conn;
      if (!connect_daemon(conn, use_tcp, tcp_host, tcp_port)) {
        std::cout << "Daemon is not running.\n";
        return 0;
      }
      // Request daemon to shut down by sending a shutdown signal
      // The daemon responds to daemon.shutdown
      std::string req = make_req("cli-stop", "daemon.shutdown", {});
      conn.send(req);
      conn.close();
      std::cout << "Daemon stopped.\n";
      return 0;
    }

    if (sub == "restart") {
      // Stop then start
      int stopResult = 0;
      {
        Conn conn;
        if (connect_daemon(conn, use_tcp, tcp_host, tcp_port)) {
          std::string req = make_req("cli-stop", "daemon.shutdown", {});
          conn.send(req);
          conn.close();
          std::cout << "Daemon stopped.\n";
          Sleep(1000);
        }
      }
      // Fall through to start
      wchar_t modulePath[MAX_PATH];
      GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
      std::wstring dir = modulePath;
      auto pos = dir.find_last_of(L"\\");
      if (pos != std::wstring::npos) dir = dir.substr(0, pos + 1);
      std::wstring daemonPath = dir + L"wininspectd.exe";
      std::wstring cmdLine = L"\"" + daemonPath + L"\" --headless";
      STARTUPINFOW si = {sizeof(si)};
      PROCESS_INFORMATION pi;
      if (CreateProcessW(daemonPath.c_str(), &cmdLine[0], nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        std::cout << "Daemon restarted.\n";
        for (int retry = 0; retry < 10; retry++) {
          Sleep(500);
          Conn c2;
          if (connect_daemon(c2, use_tcp, tcp_host, tcp_port)) {
            c2.close();
            std::cout << "Daemon ready.\n";
            return 0;
          }
        }
      }
      return 0;
    }

    return usage();
  }

  if (cmd == "control") {
    if (args.size() < 2)
      return send_and_print("control.status");
    std::string sub = args[1];
    if (sub == "take") {
      for (size_t i = 2; i < args.size(); i++) {
        if (args[i] == "--controller" && i + 1 < args.size())
          params["controller"] = args[++i];
        if (args[i] == "--id" && i + 1 < args.size())
          params["id"] = args[++i];
      }
      return send_and_print("control.take");
    }
    if (sub == "release")
      return send_and_print("control.release");
    if (sub == "mode" && args.size() > 2) {
      params["mode"] = args[2];
      return send_and_print("control.setMode");
    }
    if (sub == "audit-log")
      return send_and_print("control.auditLog");
    return usage();
  }

  if (cmd == "check-update") {
    return send_and_print("daemon.checkUpdate");
  }

  if (cmd == "update") {
    for (size_t i = 1; i < args.size(); ++i) {
      if (args[i] == "--url" && i + 1 < args.size()) {
        params["url"] = args[++i];
      }
      else if (args[i] == "--type" && i + 1 < args.size()) {
        params["type"] = args[++i];
      }
    }
    return send_and_print("daemon.downloadUpdate");
  }

  if (cmd == "session") {
    if (args.size() < 2)
      return usage();
    std::string sub = args[1];

    if (sub == "record") {
      int interval_ms = 1000;
      int max_frames = 0; // 0 = unlimited
      std::string output_path = "session.wisession";
      bool record_input = false;
      for (size_t i = 2; i < args.size(); i++) {
        if (args[i] == "--interval" && i + 1 < args.size())
          interval_ms = std::stoi(args[++i]);
        if (args[i] == "--max-frames" && i + 1 < args.size())
          max_frames = std::stoi(args[++i]);
        if (args[i] == "--output" && i + 1 < args.size())
          output_path = args[++i];
        if (args[i] == "--record-input")
          record_input = true;
      }

      Conn conn;
      if (!connect_daemon(conn, use_tcp, tcp_host, tcp_port)) {
        std::cerr << "error: could not connect to daemon\n";
        return 1;
      }

      std::cout << "Recording session to " << output_path << "\n";
      std::cout << "Press Ctrl+C to stop\n";

      // Session file header
      wininspect::json::Object session;
      session["version"] = 1.0;
      session["started_at"] = std::to_string(GetTickCount());
      session["interval_ms"] = (double)interval_ms;
      session["record_input"] = record_input;
      wininspect::json::Array frames;
      int frame_count = 0;

      auto capture_frame = [&]() -> wininspect::json::Object {
        wininspect::json::Object frame;
        frame["timestamp_ms"] = (double)GetTickCount();
        frame["frame"] = (double)(++frame_count);

        // Screen capture
        wininspect::json::Object capParams;
        capParams["left"] = 0.0;
        capParams["top"] = 0.0;
        capParams["right"] = 1920.0;
        capParams["bottom"] = 1080.0;
        std::string req = make_req("rec", "screen.capture", capParams);
        std::string resp;
        if (conn.send(req) && conn.recv(resp)) {
          try {
            auto v = wininspect::json::parse(resp);
            if (v.is_obj())
              frame["capture"] = v.as_obj();
          }
          catch (...) {
          }
        }

        // Events poll
        wininspect::json::Object evParams;
        evParams["wait_ms"] = 0.0;
        req = make_req("rec", "events.poll", evParams);
        if (conn.send(req) && conn.recv(resp)) {
          try {
            auto v = wininspect::json::parse(resp);
            if (v.is_obj())
              frame["events"] = v.as_obj();
          }
          catch (...) {
          }
        }

        return frame;
      };

      // Record loop
      while (true) {
        auto frame = capture_frame();
        frames.push_back(frame);

        if (max_frames > 0 && frame_count >= max_frames)
          break;
        Sleep(interval_ms);
      }

      session["frames"] = frames;
      session["total_frames"] = (double)frame_count;
      session["duration_ms"] =
          (double)(GetTickCount() - std::stoul(session["started_at"].as_str()));

      std::ofstream f(output_path);
      if (f.is_open()) {
        f << wininspect::json::dumps(session);
        f.close();
        std::cout << "Session saved: " << output_path << " (" << frame_count << " frames)\n";
      }
      conn.close();
      return 0;
    }

    if (sub == "replay") {
      std::string input_path;
      double speed = 1.0;
      for (size_t i = 2; i < args.size(); i++) {
        if (args[i] == "--speed" && i + 1 < args.size())
          speed = std::stod(args[++i]);
        if (i + 1 < args.size() || (i == args.size() - 1 && args[i].find("--") != 0))
          input_path = args[i];
      }
      if (input_path.empty()) {
        std::cerr << "error: specify session file\n";
        return 1;
      }

      std::ifstream f(input_path);
      if (!f.is_open()) {
        std::cerr << "error: could not open " << input_path << "\n";
        return 1;
      }
      std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      f.close();

      try {
        auto v = wininspect::json::parse(content);
        if (!v.is_obj()) {
          std::cerr << "error: invalid session file\n";
          return 1;
        }
        auto session = v.as_obj();
        auto it_frames = session.find("frames");
        if (it_frames == session.end() || !it_frames->second.is_arr()) {
          std::cerr << "error: no frames in session\n";
          return 1;
        }
        auto frames = it_frames->second.as_arr();
        std::cout << "Replaying " << frames.size() << " frames at " << speed << "x\n";

        int64_t prev_time = 0;
        for (auto& f : frames) {
          if (!f.is_obj())
            continue;
          auto fo = f.as_obj();
          auto it_ts = fo.find("timestamp_ms");
          if (it_ts != fo.end() && it_ts->second.is_num() && prev_time > 0) {
            int64_t delay = (int64_t)((it_ts->second.as_num() - prev_time) / speed);
            if (delay > 0)
              Sleep((DWORD)delay);
          }
          if (it_ts != fo.end() && it_ts->second.is_num())
            prev_time = (int64_t)it_ts->second.as_num();

          auto it_frame = fo.find("frame");
          auto it_cap = fo.find("capture");
          std::cout << "Frame "
                    << (it_frame != fo.end() ? std::to_string((int64_t)it_frame->second.as_num())
                                             : "?")
                    << ": ";
          if (it_cap != fo.end() && it_cap->second.is_obj()) {
            auto cap = it_cap->second.as_obj();
            auto ok = cap.find("ok");
            if (ok != cap.end() && ok->second.is_bool())
              std::cout << (ok->second.as_bool() ? "capture OK" : "capture FAIL");
          }
          auto it_ev = fo.find("events");
          if (it_ev != fo.end() && it_ev->second.is_obj()) {
            auto ev = it_ev->second.as_obj();
            auto result = ev.find("result");
            if (result != ev.end() && result->second.is_arr())
              std::cout << ", events: " << result->second.as_arr().size();
          }
          std::cout << "\n";
        }
        std::cout << "Replay complete (" << frames.size() << " frames)\n";
      }
      catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
      }
      return 0;
    }

    return usage();
  }

  if (cmd == "credential") {
    if (args.size() < 2)
      return send_and_print("credential.list");
    std::string sub = args[1];

    if (sub == "store") {
      std::string target, username, type = "password", file;
      for (size_t i = 2; i < args.size(); i++) {
        if (args[i] == "--target" && i + 1 < args.size())
          target = args[++i];
        if (args[i] == "--username" && i + 1 < args.size())
          username = args[++i];
        if (args[i] == "--type" && i + 1 < args.size())
          type = args[++i];
        if (args[i] == "--file" && i + 1 < args.size())
          file = args[++i];
      }
      if (target.empty() || username.empty()) {
        std::cerr << "error: --target and --username required\n";
        return 1;
      }
      std::vector<uint8_t> blob;
      if (!file.empty()) {
        std::ifstream f(file, std::ios::binary);
        if (!f) {
          std::cerr << "error: cannot read " << file << "\n";
          return 1;
        }
        blob = std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
      }
      params["target"] = target;
      params["username"] = username;
      params["type"] = type;
      params["data_b64"] = wininspect::base64::encode(blob);
      return send_and_print("credential.store");
    }

    if (sub == "retrieve") {
      std::string target, output_file;
      for (size_t i = 2; i < args.size(); i++) {
        if (args[i] == "--target" && i + 1 < args.size())
          target = args[++i];
        if (args[i] == "--output-file" && i + 1 < args.size())
          output_file = args[++i];
      }
      if (target.empty()) {
        std::cerr << "error: --target required\n";
        return 1;
      }
      params["target"] = target;
      return send_and_print("credential.retrieve");
    }

    if (sub == "delete") {
      std::string target;
      for (size_t i = 2; i < args.size(); i++) {
        if (args[i] == "--target" && i + 1 < args.size())
          target = args[++i];
      }
      if (target.empty()) {
        std::cerr << "error: --target required\n";
        return 1;
      }
      params["target"] = target;
      return send_and_print("credential.delete");
    }

    if (sub == "list") {
      return send_and_print("credential.list");
    }

    if (sub == "generate") {
      std::string target, type = "password";
      int length = 32;
      for (size_t i = 2; i < args.size(); i++) {
        if (args[i] == "--target" && i + 1 < args.size())
          target = args[++i];
        if (args[i] == "--type" && i + 1 < args.size())
          type = args[++i];
        if (args[i] == "--length" && i + 1 < args.size())
          length = std::stoi(args[++i]);
      }
      params["type"] = type;
      params["length"] = (double)length;
      if (!target.empty())
        params["target"] = target;
      return send_and_print("credential.generate");
    }

    return usage();
  }

  if (cmd == "diag") {
    // Parse flags
    bool include_logs = false;
    bool include_capture = false;
    std::string output_path;
    for (size_t i = 1; i < args.size(); i++) {
      if (args[i] == "--include-logs")
        include_logs = true;
      if (args[i] == "--include-capture")
        include_capture = true;
      if (args[i] == "--output" && i + 1 < args.size())
        output_path = args[++i];
    }

    Conn conn;
    if (!connect_daemon(conn, use_tcp, tcp_host, tcp_port)) {
      std::cerr << "error: could not connect to daemon\n";
      return 1;
    }

    wininspect::json::Object bundle;
    bundle["version"] = std::string(wininspect::WININSPECT_VERSION);
    bundle["tool"] = std::string("WinInspect Diagnostic Bundle");
    bundle["generated_at"] = std::to_string(GetTickCount());

    // Helper: query one RPC and store in bundle
    auto query = [&](const std::string& key, const std::string& method) {
      auto req = make_req("diag", method, Object{});
      std::string resp;
      if (conn.send(req) && conn.recv(resp)) {
        try {
          auto v = wininspect::json::parse(resp);
          if (v.is_obj())
            bundle[key] = v.as_obj();
        }
        catch (...) {
        }
      }
    };

    // Core diagnostics
    query("diag", "daemon.diag");

    // Audit log
    Object audit_params;
    audit_params["max"] = 100.0;
    auto req = make_req("diag", "control.auditLog", audit_params);
    std::string resp;
    if (conn.send(req) && conn.recv(resp)) {
      try {
        auto v = wininspect::json::parse(resp);
        if (v.is_obj())
          bundle["audit_log"] = v.as_obj();
      }
      catch (...) {
      }
    }

    // Logs
    if (include_logs) {
      wininspect::json::Object logParams;
      logParams["max"] = 500.0;
      auto lr = make_req("diag", "daemon.logs", logParams);
      if (conn.send(lr) && conn.recv(resp)) {
        try {
          auto v = wininspect::json::parse(resp);
          if (v.is_obj())
            bundle["logs"] = v.as_obj();
        }
        catch (...) {
        }
      }
    }

    // Screen capture
    if (include_capture) {
      wininspect::json::Object capParams;
      capParams["left"] = 0.0;
      capParams["top"] = 0.0;
      capParams["right"] = 1920.0;
      capParams["bottom"] = 1080.0;
      auto cr = make_req("diag", "screen.capture", capParams);
      if (conn.send(cr) && conn.recv(resp)) {
        try {
          auto v = wininspect::json::parse(resp);
          if (v.is_obj())
            bundle["screen_capture"] = v.as_obj();
        }
        catch (...) {
        }
      }
    }

    conn.close();

    std::string output = wininspect::json::dumps(bundle);
    if (!output_path.empty()) {
      std::ofstream f(output_path);
      if (f.is_open()) {
        f << output;
        f.close();
        std::cout << "Diagnostic bundle saved to: " << output_path << "\n";
      }
      else {
        std::cerr << "error: could not write to " << output_path << "\n";
        return 1;
      }
    }
    else {
      std::cout << output << "\n";
    }
    return 0;
  }

  if (cmd == "config") {
    if (args.size() >= 3 && args[1] == "--key") {
      save_key_path(args[2]);
      std::cout << "Key path saved: " << args[2] << "\n";
      return 0;
    }
    return usage();
  }

  if (cmd == "metrics") {
    // Query and format metric
    Conn conn;
    if (!connect_daemon(conn, use_tcp, tcp_host, tcp_port)) {
      std::cerr << "error: could not connect to daemon\n";
      return 1;
    }
    std::string req = make_req("metrics", "daemon.metrics", Object{});
    std::string resp;
    if (!conn.send(req) || !conn.recv(resp)) {
      std::cerr << "communication error\n";
      conn.close();
      return 1;
    }
    try {
      auto v = wininspect::json::parse(resp);
      if (v.is_obj()) {
        auto o = v.as_obj();
        auto it = o.find("result");
        if (it != o.end() && it->second.is_obj()) {
          auto r = it->second.as_obj();
          auto tc = r.find("total_calls");
          auto tm = r.find("total_methods");
          std::cout << "=== Performance Metrics ===" << "\n";
          if (tc != r.end())
            std::cout << "Total calls: " << (int64_t)tc->second.as_num() << "\n";
          if (tm != r.end())
            std::cout << "Active methods: " << (int64_t)tm->second.as_num() << "\n";
          auto it_m = r.find("methods");
          if (it_m != r.end() && it_m->second.is_arr()) {
            std::cout << "\nMethod                    Calls   Avg(ms)  P99(ms)  Max(ms)\n";
            std::cout << "---------------------------------------------------------\n";
            for (auto& m : it_m->second.as_arr()) {
              if (!m.is_obj())
                continue;
              auto mo = m.as_obj();
              auto method = mo.find("method");
              auto calls = mo.find("calls");
              auto avg = mo.find("avg_ms");
              auto p99 = mo.find("p99_ms");
              auto max = mo.find("max_ms");
              if (method == mo.end() || calls == mo.end())
                continue;
              char buf[128];
              snprintf(buf, sizeof(buf), "%-25s %8.0f  %8.1f  %8.1f  %8.1f",
                       method->second.as_str().c_str(), calls->second.as_num(),
                       avg != mo.end() ? avg->second.as_num() : 0.0,
                       p99 != mo.end() ? p99->second.as_num() : 0.0,
                       max != mo.end() ? max->second.as_num() : 0.0);
              std::cout << buf << "\n";
            }
          }
        }
      }
    }
    catch (...) {
      std::cerr << "error: could not parse metrics response\n";
    }
    conn.close();
    return 0;
  }

  if (cmd == "privacy-scan") {
    Conn conn;
    if (!connect_daemon(conn, use_tcp, tcp_host, tcp_port)) {
      std::cerr << "error: could not connect to daemon\n";
      return 1;
    }

    std::cout << "=== WinInspect Privacy Scan ===\n\n";

    // Helper: send RPC and parse response
    using JObj = wininspect::json::Object;
    auto query = [&](const std::string& method, JObj p) -> JObj {
      std::string req = make_req("scan", method, p);
      std::string r;
      if (!conn.send(req) || !conn.recv(r))
        return JObj{};
      try {
        auto v = wininspect::json::parse(r);
        if (v.is_obj())
          return v.as_obj();
      }
      catch (...) {
      }
      return JObj{};
    };

    // 1. Health — OS, Wine, diagnostics
    auto health = query("daemon.health", params);
    if (!health.empty()) {
      std::cout << "System:\n";
      auto it = health.find("result");
      if (it != health.end() && it->second.is_obj()) {
        auto r = it->second.as_obj();
        auto os = r.find("os");
        if (os != r.end() && os->second.is_str())
          std::cout << "  OS: " << os->second.as_str() << "\n";
        auto wine = r.find("is_wine");
        if (wine != r.end() && wine->second.is_bool())
          std::cout << "  Wine: " << (wine->second.as_bool() ? "yes" : "no") << "\n";
      }
    }

    // 2. Status — version, mode
    auto status = query("daemon.status", params);
    if (!status.empty()) {
      auto it = status.find("result");
      if (it != status.end() && it->second.is_obj()) {
        auto r = it->second.as_obj();
        auto features = r.find("features");
        if (features != r.end() && features->second.is_obj()) {
          std::cout << "\nCapabilities:\n";
          auto f = features->second.as_obj();
          for (auto& [k, v] : f) {
            std::cout << "  " << k << ": " << (v.is_bool() && v.as_bool() ? "yes" : "no") << "\n";
          }
        }
      }
    }

    // 3. Network exposure summary (from what we can infer)
    std::cout << "\nNetwork:\n";
    std::cout << "  TCP port: " << tcp_port << " (host: " << tcp_host << ")\n";
    std::cout << "  TCP auth: " << (use_tcp ? "none (plaintext)" : "local pipe only") << "\n";

    // 4. Control state
    auto ctrl = query("control.status", Object{});
    if (!ctrl.empty()) {
      auto it = ctrl.find("result");
      if (it != ctrl.end() && it->second.is_obj()) {
        auto r = it->second.as_obj();
        auto mode = r.find("operation_mode");
        if (mode != r.end() && mode->second.is_str())
          std::cout << "  Operation mode: " << mode->second.as_str() << "\n";
        auto controller = r.find("controller");
        if (controller != r.end() && controller->second.is_str())
          std::cout << "  Current controller: " << controller->second.as_str() << "\n";
      }
    }

    // 5. Recommendations
    std::cout << "\nRecommendations:\n";
    if (!use_tcp) {
      std::cout << "  ✓ Local pipe only — no network exposure\n";
    }
    else {
      std::cout << "  ⚠ TCP enabled — use --auth-keys for encryption\n";
    }
    std::cout << "  • Use --no-clipboard to block clipboard access\n";
    std::cout << "  • Use --read-only to prevent system mutations\n";
    std::cout << "  • Use --bind 127.0.0.1 to restrict to localhost\n";
    std::cout << "  • Use --enable-update-check=false to disable update checks\n";

    std::cout << "\nPrivacy policy: PRIVACY.md\n";
    conn.close();
    return 0;
  }

  return usage();
}
#else
int main()
{
  return 0;
}
#endif
