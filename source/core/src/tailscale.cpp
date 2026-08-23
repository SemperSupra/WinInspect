// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/tailscale.hpp"
#include "wininspect/logger.hpp"

#include <cstdio>
#include <memory>
#include <array>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace wininspect {

  // ── Run a command and capture stdout ────────────────────────────────────────

  static std::string exec_cmd(const std::string& cmd)
  {
    std::array<char, 256> buffer;
    std::string result;
#ifdef _WIN32
    // Use popen on Windows (works in both MSVC and MinGW)
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe)
      return result;
    while (fgets(buffer.data(), (int)buffer.size(), pipe) != nullptr)
      result += buffer.data();
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return result;
  }

  // ── Tailscale Up ────────────────────────────────────────────────────────────

  bool tailscale_up(const std::string& auth_key)
  {
    std::string cmd = "tailscale up --auth-key " + auth_key + " 2>&1";
    LOG_INFO("Tailscale: joining tailnet...");
    auto output = exec_cmd(cmd);
    if (!output.empty())
      LOG_DEBUG("tailscale up output: " + output.substr(0, 200));

    // Give tailscale a moment to establish the connection
    std::this_thread::sleep_for(std::chrono::seconds(3));

    if (tailscale_is_running()) {
      auto ip = tailscale_ip();
      LOG_INFO("Tailscale: connected with IP " + (ip.value_or("unknown")));
      return true;
    }

    LOG_WARN("Tailscale: failed to join tailnet");
    return false;
  }

  // ── Tailscale IP ────────────────────────────────────────────────────────────

  std::optional<std::string> tailscale_ip()
  {
    auto output = exec_cmd("tailscale status 2>&1");

    // Parse `tailscale status` output format:
    // 100.x.x.x    hostname        username@domain    Windows
    // We want the first IP in the output (our own)

    size_t pos = 0;
    while (pos < output.size()) {
      // Look for a 100.x.x.x or 100.x.x.x pattern
      if (output.substr(pos, 4) == "100." ||
          (pos + 3 < output.size() && output.substr(pos, 4) == "100.")) {
        size_t end = pos;
        while (end < output.size() && output[end] != ' ' && output[end] != '\t' &&
               output[end] != '\n')
          end++;
        return output.substr(pos, end - pos);
      }
      pos = output.find('\n', pos);
      if (pos == std::string::npos)
        break;
      pos++;
    }

    return std::nullopt;
  }

  // ── Tailscale Running Check ─────────────────────────────────────────────────

  bool tailscale_is_running()
  {
    auto output = exec_cmd("tailscale status 2>&1");
    // If tailscale is running, it outputs status lines
    // If not running, it outputs an error message
    return !output.empty() && output.find("not") == std::string::npos;
  }

} // namespace wininspect
