// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/network_config.hpp"
#include "wininspect/logger.hpp"
#include "wininspect/base64.hpp"
#include "wininspect/crypto.hpp"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace wininspect {

// ══════════════════════════════════════════════════════════════════════════════
// UUID Generation (RFC 4122 v4)
// ══════════════════════════════════════════════════════════════════════════════

std::string generate_uuid_v4() {
#ifdef _WIN32
  uint8_t bytes[16];
  NTSTATUS status = BCryptGenRandom(nullptr, bytes, (ULONG)sizeof(bytes),
                                     BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  if (status < 0) {
    LOG_ERROR("BCryptGenRandom failed in generate_uuid_v4");
    return "00000000-0000-4000-8000-000000000000";
  }
#else
  // POSIX fallback (Wine/Linux) — read from /dev/urandom
  uint8_t bytes[16] = {};
  FILE *f = fopen("/dev/urandom", "rb");
  if (f) {
    (void)fread(bytes, 1, sizeof(bytes), f);
    fclose(f);
  }
#endif

  // RFC 4122 v4: set version bits (4xxx) and variant bits (10xx)
  bytes[6] = (bytes[6] & 0x0f) | 0x40;   // version 4
  bytes[8] = (bytes[8] & 0x3f) | 0x80;   // variant RFC 4122

  char buf[37];
  std::snprintf(buf, sizeof(buf),
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    bytes[0], bytes[1], bytes[2], bytes[3],
    bytes[4], bytes[5], bytes[6], bytes[7],
    bytes[8], bytes[9], bytes[10], bytes[11],
    bytes[12], bytes[13], bytes[14], bytes[15]);
  return std::string(buf);
}

// ══════════════════════════════════════════════════════════════════════════════
// ECDH Public Key Generation
// ══════════════════════════════════════════════════════════════════════════════

std::string generate_ecdh_pubkey() {
  // Use the existing CryptoSession infrastructure
  // (crypto.cpp has generate_local_key() which returns raw bytes)
  // We just return the base64 encoding of a fresh key
  crypto::CryptoSession cs;
  auto pubkey = cs.generate_local_key();
  if (pubkey.empty()) {
    LOG_DEBUG("ECDH key generation failed (no-op in stub context)");
    return "";
  }
  return base64::encode(pubkey);
}

// ══════════════════════════════════════════════════════════════════════════════
// Default Config Paths
// ══════════════════════════════════════════════════════════════════════════════

std::string default_config_dir() {
#ifdef _WIN32
  char *appdata = nullptr;
  size_t sz = 0;
  if (_dupenv_s(&appdata, &sz, "APPDATA") == 0 && appdata) {
    std::string dir = std::string(appdata) + "\\WinInspect";
    free(appdata);
    return dir;
  }
  return std::string(getenv("USERPROFILE")) + "\\.config\\wininspect";
#else
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0]) return std::string(xdg) + "/wininspect";
  const char *home = getenv("HOME");
  if (home) return std::string(home) + "/.config/wininspect";
  return "./.config/wininspect";
#endif
}

std::string default_config_path() {
  return default_config_dir() + "/config.json";
}

// ══════════════════════════════════════════════════════════════════════════════
// Serialization (to_json / from_json)
// ══════════════════════════════════════════════════════════════════════════════

json::Object InstanceIdentity::to_json() const {
  json::Object o;
  o["uuid"] = uuid;
  o["name"] = name;
  o["hostname"] = hostname;
  if (!ecdh_pubkey.empty()) o["ecdh_pubkey"] = ecdh_pubkey;
  return o;
}

InstanceIdentity InstanceIdentity::from_json(const json::Object &o) {
  InstanceIdentity id;
  auto it = o.find("uuid");
  if (it != o.end() && it->second.is_str()) id.uuid = it->second.as_str();
  it = o.find("name");
  if (it != o.end() && it->second.is_str()) id.name = it->second.as_str();
  it = o.find("hostname");
  if (it != o.end() && it->second.is_str()) id.hostname = it->second.as_str();
  it = o.find("ecdh_pubkey");
  if (it != o.end() && it->second.is_str()) id.ecdh_pubkey = it->second.as_str();
  return id;
}

json::Object NetworkAddress::to_json() const {
  json::Object o;
  o["address"] = address;
  if (family == ADDR_FAMILY_IPV4) o["family"] = std::string("ipv4");
  else if (family == ADDR_FAMILY_IPV6) o["family"] = std::string("ipv6");
  else o["family"] = std::string("dual");
  if (scope_id != 0) o["scope_id"] = (double)scope_id;
  return o;
}

NetworkAddress NetworkAddress::from_json(const json::Object &o) {
  NetworkAddress addr;
  auto it = o.find("address");
  if (it != o.end() && it->second.is_str()) addr.address = it->second.as_str();
  it = o.find("family");
  if (it != o.end() && it->second.is_str()) {
    auto fam = it->second.as_str();
    if (fam == "ipv4") addr.family = ADDR_FAMILY_IPV4;
    else if (fam == "ipv6") addr.family = ADDR_FAMILY_IPV6;
    else addr.family = ADDR_FAMILY_UNSPEC;
  }
  it = o.find("scope_id");
  if (it != o.end() && it->second.is_num()) addr.scope_id = (int)it->second.as_num();
  return addr;
}

json::Object RendezvousConfig::to_json() const {
  json::Object o;
  o["url"] = url;
  o["heartbeat_sec"] = (double)heartbeat_sec;
  if (!domain_uuid.empty()) o["domain_uuid"] = domain_uuid;
  if (!domain_nickname.empty()) o["domain_nickname"] = domain_nickname;
  // crypto_key is intentionally NOT serialized to disk for security.
  return o;
}

RendezvousConfig RendezvousConfig::from_json(const json::Object &o) {
  RendezvousConfig rc;
  auto it = o.find("url");
  if (it != o.end() && it->second.is_str()) rc.url = it->second.as_str();
  it = o.find("crypto_key");
  if (it != o.end() && it->second.is_str()) rc.crypto_key = it->second.as_str();
  it = o.find("domain_uuid");
  if (it != o.end() && it->second.is_str()) rc.domain_uuid = it->second.as_str();
  it = o.find("domain_nickname");
  if (it != o.end() && it->second.is_str()) rc.domain_nickname = it->second.as_str();
  it = o.find("heartbeat_sec");
  if (it != o.end() && it->second.is_num()) rc.heartbeat_sec = (int)it->second.as_num();
  return rc;
}

json::Object NetworkConfig::to_json() const {
  json::Object o;
  o["identity"] = identity.to_json();
  json::Array bind_arr;
  for (auto &b : bind) bind_arr.push_back(b.to_json());
  o["bind"] = bind_arr;
  o["port"] = (double)port;
  o["discovery_port"] = (double)discovery_port;
  json::Array rv_arr;
  for (auto &r : rendezvous) rv_arr.push_back(r.to_json());
  o["rendezvous"] = rv_arr;
  o["request_timeout_ms"] = (double)request_timeout_ms;
  o["rate_limit_ms"] = (double)rate_limit_ms;
  o["include_hostname"] = include_hostname;
  o["enable_update_check"] = enable_update_check;
  o["update_check_interval_hours"] = (double)update_check_interval_hours;
  return o;
}

NetworkConfig NetworkConfig::from_json(const json::Object &o) {
  NetworkConfig cfg;
  auto it = o.find("identity");
  if (it != o.end() && it->second.is_obj())
    cfg.identity = InstanceIdentity::from_json(it->second.as_obj());

  it = o.find("bind");
  if (it != o.end() && it->second.is_arr()) {
    cfg.bind.clear();
    for (auto &v : it->second.as_arr()) {
      if (v.is_obj()) cfg.bind.push_back(NetworkAddress::from_json(v.as_obj()));
    }
  }

  it = o.find("port");
  if (it != o.end() && it->second.is_num()) cfg.port = (int)it->second.as_num();
  it = o.find("discovery_port");
  if (it != o.end() && it->second.is_num()) cfg.discovery_port = (int)it->second.as_num();

  it = o.find("rendezvous");
  if (it != o.end() && it->second.is_arr()) {
    for (auto &v : it->second.as_arr()) {
      if (v.is_obj()) cfg.rendezvous.push_back(RendezvousConfig::from_json(v.as_obj()));
    }
  }

  it = o.find("request_timeout_ms");
  if (it != o.end() && it->second.is_num()) cfg.request_timeout_ms = (int)it->second.as_num();
  it = o.find("rate_limit_ms");
  if (it != o.end() && it->second.is_num()) cfg.rate_limit_ms = (int)it->second.as_num();
  it = o.find("include_hostname");
  if (it != o.end() && it->second.is_bool()) cfg.include_hostname = it->second.as_bool();
  it = o.find("enable_update_check");
  if (it != o.end() && it->second.is_bool()) cfg.enable_update_check = it->second.as_bool();
  it = o.find("update_check_interval_hours");
  if (it != o.end() && it->second.is_num()) cfg.update_check_interval_hours = (int)it->second.as_num();

  return cfg;
}

// ══════════════════════════════════════════════════════════════════════════════
// Config File I/O
// ══════════════════════════════════════════════════════════════════════════════

static void ensure_dir_exists(const std::string &path) {
#ifdef _WIN32
  // CreateDirectoryA creates intermediate components? No — just the last component.
  // Use a simple approach: try to create, ignore if exists
  CreateDirectoryA(path.c_str(), nullptr);
#else
  mkdir(path.c_str(), 0755);
#endif
}

static std::string read_file(const std::string &path) {
  std::ifstream f(path);
  if (!f.is_open()) return "";
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static bool write_file(const std::string &path, const std::string &content) {
  std::ofstream f(path);
  if (!f.is_open()) return false;
  f << content;
  return f.good();
}

NetworkConfig load_config(const std::string &path) {
  NetworkConfig cfg;
  auto content = read_file(path);
  if (content.empty()) {
    LOG_DEBUG("No config file at " + path + " — using defaults");
    return cfg;
  }
  try {
    auto v = json::parse(content);
    if (v.is_obj()) {
      cfg = NetworkConfig::from_json(v.as_obj());
      LOG_DEBUG("Loaded config from " + path);
    }
  } catch (const std::exception &e) {
    LOG_ERROR("Failed to parse config file " + path + ": " + e.what());
  }
  return cfg;
}

void save_config(const std::string &path, const NetworkConfig &cfg) {
  auto dir = path.substr(0, path.find_last_of("/\\"));
  ensure_dir_exists(dir);
  auto content = json::dumps(cfg.to_json());
  if (write_file(path, content)) {
    LOG_DEBUG("Saved config to " + path);
  } else {
    LOG_ERROR("Failed to write config to " + path);
  }
}

InstanceIdentity load_or_create_identity(const std::string &config_dir) {
  ensure_dir_exists(config_dir);
  std::string id_path = config_dir + "/instance.id";
  auto content = read_file(id_path);
  if (!content.empty()) {
    // Trim whitespace
    content.erase(std::remove_if(content.begin(), content.end(),
                                  [](char c) { return c == '\r' || c == '\n' || c == ' '; }),
                  content.end());
    // Validate UUID format
    std::regex uuid_regex("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-4[0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}");
    if (std::regex_match(content, uuid_regex)) {
      InstanceIdentity id;
      id.uuid = content;
      // Try to load the full config for name/hostname/ecdh_pubkey
      auto cfg = load_config(default_config_path());
      id.name = cfg.identity.name;
      id.hostname = cfg.identity.hostname;
      id.ecdh_pubkey = cfg.identity.ecdh_pubkey;
      return id;
    }
    LOG_WARN("Invalid UUID in " + id_path + " — regenerating");
  }

  // First run or corrupt file — generate fresh identity
  InstanceIdentity id;
  id.uuid = generate_uuid_v4();

  // Get hostname
  char hostname_buf[256] = {};
  if (gethostname(hostname_buf, sizeof(hostname_buf)) == 0)
    id.hostname = hostname_buf;

  id.name = id.hostname;  // default name is hostname
  id.ecdh_pubkey = generate_ecdh_pubkey();

  // Write UUID file
  write_file(id_path, id.uuid + "\n");

  // Save full config
  NetworkConfig cfg;
  cfg.identity = id;
  save_config(default_config_path(), cfg);

  LOG_INFO("Generated new instance identity: " + id.uuid);
  return id;
}

} // namespace wininspect
