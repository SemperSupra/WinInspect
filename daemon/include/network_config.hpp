#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/network_config.hpp"

namespace wininspectd {

/// Parse CLI flags and merge them over a loaded NetworkConfig.
/// Flags that are not set leave the config value unchanged (from file).
/// Flags that are set override the config value.
///
/// Supports:
///   --bind <address>        (can be specified multiple times)
///   --ipv4                  (pin to IPv4 only, overrides bind family)
///   --ipv6                  (pin to IPv6 only, overrides bind family)
///   --port <n>              (TCP port)
///   --discovery-port <n>    (UDP discovery port)
///   --instance-name <s>     (override identity name in config)
///   --rendezvous <url>      (add rendezvous endpoint)
///   --rendezvous-key <key>  (set rendezvous crypto key)
///   --config <path>         (config file path)
///   --no-config             (don't read/write config)
///   --include-hostname     (existing flag)
///   --rate-limit-ms <n>
///   --request-timeout <n>
wininspect::NetworkConfig apply_cli_overrides(
    const wininspect::NetworkConfig &base,
    int argc, char **argv);

} // namespace wininspectd
