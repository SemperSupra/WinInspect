#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include <string>
#include <optional>

namespace wininspect {

  /// Run `tailscale up` to join a tailnet with the given auth key.
  /// Returns true if the command succeeded.
  bool tailscale_up(const std::string& auth_key);

  /// Get the Tailscale IP address (100.x.x.x) from `tailscale status`.
  /// Returns the IP string or nullopt if tailscale is not running.
  std::optional<std::string> tailscale_ip();

  /// Run `tailscale status` and parse the output.
  /// Returns true if tailscale appears to be running.
  bool tailscale_is_running();

} // namespace wininspect
