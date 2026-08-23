#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "types.hpp"
#include "tinyjson.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace wininspect {

  // Config file schema version. Increment when the JSON schema changes.
  // Migration code in from_json() handles upgrades.
  inline constexpr int CONFIG_VERSION = 1;
  static_assert(CONFIG_VERSION > 0 && CONFIG_VERSION < 1000,
                "CONFIG_VERSION must be between 1 and 999");

  // ── Configuration Resolution ──────────────────────────────────────────────
  // Precedence: CLI flag > env var > config file > registry > built-in default.
  // Pattern used by cmake, git, curl, and other professional tools.
  std::string get_env(const std::string& name);
  std::string get_registry(const std::string& key);

  inline std::string resolve_config(const std::string& cli_val, const std::string& env_name,
                                    const std::string& cfg_val, const std::string& default_val)
  {
    if (!cli_val.empty())
      return cli_val;
    auto e = get_env(env_name);
    if (!e.empty())
      return e;
    auto r = get_registry(env_name);
    if (!r.empty())
      return r;
    if (!cfg_val.empty())
      return cfg_val;
    return default_val;
  }

  inline int resolve_int(const std::string& cli_val, const std::string& env_name, int cfg_val,
                         int default_val)
  {
    if (!cli_val.empty())
      return std::stoi(cli_val);
    auto e = get_env(env_name);
    if (!e.empty())
      return std::stoi(e);
    auto r = get_registry(env_name);
    if (!r.empty())
      return std::stoi(r);
    return cfg_val ? cfg_val : default_val;
  }

  // Address family constants (match Winsock AF_* values, no header dependency)
  inline constexpr int ADDR_FAMILY_UNSPEC = 0; // dual-stack (both)
  inline constexpr int ADDR_FAMILY_IPV4 = 2;   // AF_INET
  inline constexpr int ADDR_FAMILY_IPV6 = 23;  // AF_INET6

  // ── Network Address ─────────────────────────────────────────────────────────

  struct NetworkAddress
  {
    std::string address;             // "::", "0.0.0.0", "192.168.1.50", "eth0"
    int family = ADDR_FAMILY_UNSPEC; // ADDR_FAMILY_UNSPEC=dual, AF_INET=v4, AF_INET6=v6
    int scope_id = 0;                // for link-local IPv6

    json::Object to_json() const;
    static NetworkAddress from_json(const json::Object& o);
  };

  // ── Rendezvous Configuration ────────────────────────────────────────────────

  struct RendezvousConfig
  {
    std::string url;             // "https://rendezvous:8080/api/v1"
    std::string crypto_key;      // base64 HMAC key for auth
    std::string domain_uuid;     // rendezvous domain identifier (stable)
    std::string domain_nickname; // human-readable domain name (changeable)
    int heartbeat_sec = 30;

    json::Object to_json() const;
    static RendezvousConfig from_json(const json::Object& o);
  };

  // ── Network Configuration ───────────────────────────────────────────────────

  struct NetworkConfig
  {
    InstanceIdentity identity;
    std::vector<NetworkAddress> bind = {{"127.0.0.1", ADDR_FAMILY_IPV4}, {"::1", ADDR_FAMILY_IPV6}};
    int port = 1985;
    int discovery_port = 1986;
    int http_port = 0;  // 0 = disabled
    int https_port = 0; // 0 = disabled
    int tls_port = 0;   // 0 = disabled (TLS-wrapped TCP protocol)
    std::vector<RendezvousConfig> rendezvous;
    int request_timeout_ms = 5000;
    int rate_limit_ms = 0;
    int tcp_idle_timeout_ms = 1800000; // 30 min (SO_RCVTIMEO)
    int http_accept_sleep_ms = 100;    // accept() retry interval
    int cleanup_interval_ms = 30000;   // session cleanup interval
    bool enable_mdns = false;          // enable mDNS responder (opt-in)
    bool enable_discovery = false;     // enable UDP discovery responder (opt-in)
    bool include_hostname = false;

    // Tailscale IP (auto-detected at startup for cross-subnet discovery)
    std::string tailscale_ip;

    // Auto-update
    bool enable_update_check = true;
    int update_check_interval_hours = 24;

    // License and deployment model (set by installer, read on startup)
    std::string deployment = "interactive";   // "interactive" or "fleet"
    std::string license_type = "noncommercial"; // "noncommercial" or "commercial"

    json::Object to_json() const;
    static NetworkConfig from_json(const json::Object& o);
  };

  // ── Config I/O ──────────────────────────────────────────────────────────────

  /// Platform-appropriate config directory:
  ///   Windows: %APPDATA%/WinInspect/
  ///   Linux/Wine: ~/.config/wininspect/
  std::string default_config_dir();

  /// Full path to config file: <config_dir>/config.json
  std::string default_config_path();

  /// Load config from a JSON file. Returns default config if file doesn't exist
  /// or is malformed (logs error on malformed).
  NetworkConfig load_config(const std::string& path);

  /// Save config to a JSON file. Creates directory if needed.
  void save_config(const std::string& path, const NetworkConfig& cfg);

  /// Load existing identity or generate a new one (RFC 4122 v4) on first run.
  /// Identity file: <config_dir>/instance.id (simple text file, just UUID + newline)
  /// Also loads/creates the full config.json.
  InstanceIdentity load_or_create_identity(const std::string& config_dir);

  /// Generate an RFC 4122 version 4 UUID string.
  /// Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
  std::string generate_uuid_v4();

  /// Generate an ECDH keypair and return base64-encoded public key.
  /// Uses the existing crypto::CryptoSession infrastructure.
  std::string generate_ecdh_pubkey();

} // namespace wininspect
