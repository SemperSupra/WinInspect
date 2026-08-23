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

    // Auto-detect container/WSL2 IP if no explicit --bind was given
    // In Docker/WSL2, the hostname resolves to the container's bridge IP,
    // which is reachable from the host via port mapping.
    // On native Windows, the hostname typically resolves to a public or private IP.
    if (!has_bind_flag) {
      WSADATA wsa;
      if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
        char hostname[256] = {};
        if (gethostname(hostname, sizeof(hostname)) == 0) {
          struct addrinfo hints = {};
          hints.ai_family = AF_INET;
          hints.ai_socktype = SOCK_STREAM;
          struct addrinfo* result = nullptr;
          if (getaddrinfo(hostname, nullptr, &hints, &result) == 0 && result) {
            auto* sa = (struct sockaddr_in*)result->ai_addr;
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
            std::string ip_str(ip);
            // Only auto-bind to private IPs (10.x, 172.16-31.x, 192.168.x)
            // Avoids binding to 127.0.0.1 when the hostname resolves to loopback
            if (ip_str.rfind("10.", 0) == 0 ||
                (ip_str.rfind("172.", 0) == 0 && ip_str.size() > 6) ||
                ip_str.rfind("192.168.", 0) == 0) {
              LOG_INFO("Auto-detected container IP: " + ip_str);
              cfg.bind.clear();
              wininspect::NetworkAddress addr;
              addr.address = ip_str;
              addr.family = wininspect::ADDR_FAMILY_IPV4;
              cfg.bind.push_back(addr);
              has_bind_flag = true;
            }
            freeaddrinfo(result);
          }
        }
        WSACleanup();
      }
    }

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
