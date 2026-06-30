// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "network_config.hpp"
#include "wininspect/logger.hpp"

#include <string>
#include <cstring>

namespace wininspectd {

wininspect::NetworkConfig apply_cli_overrides(
    const wininspect::NetworkConfig &base,
    int argc, char **argv) {

  wininspect::NetworkConfig cfg = base;
  bool has_bind_flag = false;
  bool has_ipv4 = false;
  bool has_ipv6 = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);

    if (arg == "--bind" && i + 1 < argc) {
      if (!has_bind_flag) {
        cfg.bind.clear();
        has_bind_flag = true;
      }
      wininspect::NetworkAddress addr;
      addr.address = argv[++i];
      addr.family = wininspect::ADDR_FAMILY_UNSPEC;
      cfg.bind.push_back(addr);
      continue;
    }
    if (arg == "--ipv4") {
      has_ipv4 = true;
      continue;
    }
    if (arg == "--ipv6") {
      has_ipv6 = true;
      continue;
    }
    if (arg == "--port" && i + 1 < argc) {
      cfg.port = std::stoi(argv[++i]);
      continue;
    }
    if (arg == "--discovery-port" && i + 1 < argc) {
      cfg.discovery_port = std::stoi(argv[++i]);
      continue;
    }
    if (arg == "--instance-name" && i + 1 < argc) {
      cfg.identity.name = argv[++i];
      continue;
    }
    if (arg == "--rendezvous" && i + 1 < argc) {
      wininspect::RendezvousConfig rv;
      rv.url = argv[++i];
      cfg.rendezvous.push_back(rv);
      continue;
    }
    if (arg == "--rendezvous-key" && i + 1 < argc) {
      if (!cfg.rendezvous.empty()) {
        cfg.rendezvous.back().crypto_key = argv[++i];
      }
      continue;
    }
    if (arg == "--config" && i + 1 < argc) {
      // Config path is handled in main(), but we note it here.
      // Actual file path is passed separately.
      continue;
    }
    if (arg == "--no-config") {
      // Handled in main() — skip config file entirely.
      continue;
    }
    if (arg == "--include-hostname") {
      cfg.include_hostname = true;
      continue;
    }
    if (arg == "--rate-limit-ms" && i + 1 < argc) {
      cfg.rate_limit_ms = std::stoi(argv[++i]);
      continue;
    }
    if (arg == "--request-timeout" && i + 1 < argc) {
      cfg.request_timeout_ms = std::stoi(argv[++i]);
      continue;
    }
  }

  // Apply IPv4/IPv6 pinning to all bind addresses
  if (has_ipv4) {
    for (auto &addr : cfg.bind) {
      addr.family = wininspect::ADDR_FAMILY_IPV4;
      if (addr.address == "::") addr.address = "0.0.0.0";
    }
  } else if (has_ipv6) {
    for (auto &addr : cfg.bind) {
      addr.family = wininspect::ADDR_FAMILY_IPV6;
      if (addr.address == "0.0.0.0") addr.address = "::";
    }
  }

  return cfg;
}

} // namespace wininspectd
