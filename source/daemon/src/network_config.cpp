// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "network_config.hpp"
#include "wininspect/logger.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <string>
#include <cstring>

namespace wininspectd {

  bool is_loopback_bind_address(const std::string& address)
  {
    if (_stricmp(address.c_str(), "localhost") == 0)
      return true;

    IN_ADDR ipv4{};
    if (InetPtonA(AF_INET, address.c_str(), &ipv4) == 1) {
      const uint32_t host_order = ntohl(ipv4.S_un.S_addr);
      return ((host_order >> 24) & 0xFFu) == 127u;
    }

    IN6_ADDR ipv6{};
    if (InetPtonA(AF_INET6, address.c_str(), &ipv6) == 1) {
      const auto* bytes = reinterpret_cast<const unsigned char*>(&ipv6);
      for (int i = 0; i < 15; ++i) {
        if (bytes[i] != 0)
          return false;
      }
      return bytes[15] == 1;
    }

    return false;
  }

  bool is_unauthenticated_tcp_bind_allowed(const std::string& address,
                                           bool allow_nonloopback)
  {
    return allow_nonloopback || is_loopback_bind_address(address);
  }

  wininspect::NetworkConfig apply_cli_overrides(const wininspect::NetworkConfig& base, int argc,
                                                char** argv)
  {

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
      if (arg == "--http-port" && i + 1 < argc) {
        cfg.http_port = std::stoi(argv[++i]);
        continue;
      }
      if (arg == "--https-port" && i + 1 < argc) {
        cfg.https_port = std::stoi(argv[++i]);
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
      if (arg == "--tcp-idle-timeout" && i + 1 < argc) {
        cfg.tcp_idle_timeout_ms = std::stoi(argv[++i]);
        continue;
      }
      if (arg == "--http-accept-sleep-ms" && i + 1 < argc) {
        cfg.http_accept_sleep_ms = std::stoi(argv[++i]);
        continue;
      }
      if (arg == "--cleanup-interval" && i + 1 < argc) {
        cfg.cleanup_interval_ms = std::stoi(argv[++i]);
        continue;
      }
      if (arg == "--no-mdns") {
        cfg.enable_mdns = false;
        continue;
      }
      if (arg == "--no-discovery") {
        cfg.enable_discovery = false;
        continue;
      }
    }

    // Preserve the configured/default bind set unless the operator explicitly
    // overrides it. In particular, do not widen loopback defaults to a
    // hostname-derived RFC1918 address implicitly.

    // Env var + registry fallback for unset values (CLI > env > registry > config)
    // WININSPECT_ prefix convention: WININSPECT_PORT, WININSPECT_BIND, etc.
    if (!has_bind_flag) {
      auto bind_env = wininspect::get_env("WININSPECT_BIND");
      if (!bind_env.empty()) {
        cfg.bind.clear();
        wininspect::NetworkAddress addr;
        addr.address = bind_env;
        addr.family = wininspect::ADDR_FAMILY_UNSPEC;
        cfg.bind.push_back(addr);
      }
    }
    if (!has_ipv4 && !has_ipv6) {
      auto ipv_env = wininspect::get_env("WININSPECT_IPV4");
      if (ipv_env == "1" || ipv_env == "true")
        has_ipv4 = true;
      auto ipv6_env = wininspect::get_env("WININSPECT_IPV6");
      if (ipv6_env == "1" || ipv6_env == "true")
        has_ipv6 = true;
    }

    // Apply IPv4/IPv6 pinning to all bind addresses
    if (has_ipv4) {
      for (auto& addr : cfg.bind) {
        addr.family = wininspect::ADDR_FAMILY_IPV4;
        if (addr.address == "::")
          addr.address = "0.0.0.0";
      }
    }
    else if (has_ipv6) {
      for (auto& addr : cfg.bind) {
        addr.family = wininspect::ADDR_FAMILY_IPV6;
        if (addr.address == "0.0.0.0")
          addr.address = "::";
      }
    }

    return cfg;
  }

} // namespace wininspectd
