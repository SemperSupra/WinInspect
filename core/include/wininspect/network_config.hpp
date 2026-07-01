#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "types.hpp"
#include "tinyjson.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace wininspect {

// Address family constants (match Winsock AF_* values, no header dependency)
inline constexpr int ADDR_FAMILY_UNSPEC = 0;  // dual-stack (both)
inline constexpr int ADDR_FAMILY_IPV4 = 2;    // AF_INET
inline constexpr int ADDR_FAMILY_IPV6 = 23;   // AF_INET6

// ── Network Address ─────────────────────────────────────────────────────────

struct NetworkAddress {
  std::string address;        // "::", "0.0.0.0", "192.168.1.50", "eth0"
  int family = ADDR_FAMILY_UNSPEC;     // ADDR_FAMILY_UNSPEC=dual, AF_INET=v4, AF_INET6=v6
  int scope_id = 0;           // for link-local IPv6

  json::Object to_json() const;
  static NetworkAddress from_json(const json::Object &o);
};

// ── Rendezvous Configuration ────────────────────────────────────────────────

struct RendezvousConfig {
  std::string url;             // "https://rendezvous:8080/api/v1"
  std::string crypto_key;      // base64 HMAC key for auth
  std::string domain_uuid;     // rendezvous domain identifier (stable)
  std::string domain_nickname; // human-readable domain name (changeable)
  int heartbeat_sec = 30;

  json::Object to_json() const;
  static RendezvousConfig from_json(const json::Object &o);
};

// ── Network Configuration ───────────────────────────────────────────────────

struct NetworkConfig {
  InstanceIdentity identity;
  std::vector<NetworkAddress> bind = {{"::", ADDR_FAMILY_UNSPEC}};
  int port = 1985;
  int discovery_port = 1986;
  std::vector<RendezvousConfig> rendezvous;
  int request_timeout_ms = 5000;
  int rate_limit_ms = 0;
  bool include_hostname = false;

  // Auto-update
  bool enable_update_check = true;
  int update_check_interval_hours = 24;

  json::Object to_json() const;
  static NetworkConfig from_json(const json::Object &o);
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
NetworkConfig load_config(const std::string &path);

/// Save config to a JSON file. Creates directory if needed.
void save_config(const std::string &path, const NetworkConfig &cfg);

/// Load existing identity or generate a new one (RFC 4122 v4) on first run.
/// Identity file: <config_dir>/instance.id (simple text file, just UUID + newline)
/// Also loads/creates the full config.json.
InstanceIdentity load_or_create_identity(const std::string &config_dir);

/// Generate an RFC 4122 version 4 UUID string.
/// Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
std::string generate_uuid_v4();

/// Generate an ECDH keypair and return base64-encoded public key.
/// Uses the existing crypto::CryptoSession infrastructure.
std::string generate_ecdh_pubkey();

} // namespace wininspect
