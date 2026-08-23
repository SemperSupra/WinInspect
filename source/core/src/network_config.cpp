// SPDX-License-Identifier: PolyForm-NC-1.0.0
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
#include <filesystem>
#include <system_error>
#include <sstream>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace wininspect {

  // ── Config Resolution Helpers ──────────────────────────────────────────────
  // Precedence: CLI > env > config file > registry > default

  std::string get_env(const std::string& name)
  {
    const char* v = getenv(name.c_str());
    return v ? std::string(v) : "";
  }

  std::string get_registry(const std::string& key)
  {
#ifdef _WIN32
    HKEY hKey;
    std::string path = "Software\\WinInspect\\" + key;
    std::wstring wpath(path.begin(), path.end());
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, wpath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
      wchar_t buf[256] = {};
      DWORD size = sizeof(buf);
      DWORD type = 0;
      if (RegQueryValueExW(hKey, L"", nullptr, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        std::wstring ws(buf);
        return std::string(ws.begin(), ws.end());
      }
      RegCloseKey(hKey);
    }
#else
    (void)key;
#endif
    return "";
  }

  // ══════════════════════════════════════════════════════════════════════════════
  // UUID Generation (RFC 4122 v4)
  // ══════════════════════════════════════════════════════════════════════════════

  std::string generate_uuid_v4()
  {
#ifdef _WIN32
    uint8_t bytes[16];
    NTSTATUS status =
        BCryptGenRandom(nullptr, bytes, (ULONG)sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
      LOG_ERROR("BCryptGenRandom failed in generate_uuid_v4");
      return "00000000-0000-4000-8000-000000000000";
    }
#else
    // POSIX fallback (Wine/Linux) — read from /dev/urandom
    uint8_t bytes[16] = {};
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
      (void)fread(bytes, 1, sizeof(bytes), f);
      fclose(f);
    }
#endif

    // RFC 4122 v4: set version bits (4xxx) and variant bits (10xx)
    bytes[6] = (bytes[6] & 0x0f) | 0x40; // version 4
    bytes[8] = (bytes[8] & 0x3f) | 0x80; // variant RFC 4122

    char buf[37];
    std::snprintf(buf, sizeof(buf),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", bytes[0],
                  bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8],
                  bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return std::string(buf);
  }

  // ══════════════════════════════════════════════════════════════════════════════
  // ECDH Public Key Generation
  // ══════════════════════════════════════════════════════════════════════════════

  std::string generate_ecdh_pubkey()
  {
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

  std::string default_config_dir()
  {
#ifdef _WIN32
    char* appdata = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&appdata, &sz, "APPDATA") == 0 && appdata) {
      std::string dir = std::string(appdata) + "\\WinInspect";
      free(appdata);
      return dir;
    }
    return std::string(getenv("USERPROFILE")) + "\\.config\\wininspect";
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0])
      return std::string(xdg) + "/wininspect";
    const char* home = getenv("HOME");
    if (home)
      return std::string(home) + "/.config/wininspect";
    return "./.config/wininspect";
#endif
  }

  std::string default_config_path()
  {
    return default_config_dir() + "/config.json";
  }

  // ══════════════════════════════════════════════════════════════════════════════
  // Serialization (to_json / from_json)
  // ══════════════════════════════════════════════════════════════════════════════

  json::Object InstanceIdentity::to_json() const
  {
    json::Object o;
    o["uuid"] = uuid;
    o["name"] = name;
    o["hostname"] = hostname;
    if (!ecdh_pubkey.empty())
      o["ecdh_pubkey"] = ecdh_pubkey;
    return o;
  }

  InstanceIdentity InstanceIdentity::from_json(const json::Object& o)
  {
    InstanceIdentity id;
    auto it = o.find("uuid");
    if (it != o.end() && it->second.is_str())
      id.uuid = it->second.as_str();
    it = o.find("name");
    if (it != o.end() && it->second.is_str())
      id.name = it->second.as_str();
    it = o.find("hostname");
    if (it != o.end() && it->second.is_str())
      id.hostname = it->second.as_str();
    it = o.find("ecdh_pubkey");
    if (it != o.end() && it->second.is_str())
      id.ecdh_pubkey = it->second.as_str();
    return id;
  }

  json::Object NetworkAddress::to_json() const
  {
    json::Object o;
    o["address"] = address;
    if (family == ADDR_FAMILY_IPV4)
      o["family"] = std::string("ipv4");
    else if (family == ADDR_FAMILY_IPV6)
      o["family"] = std::string("ipv6");
    else
      o["family"] = std::string("dual");
    if (scope_id != 0)
      o["scope_id"] = (double)scope_id;
    return o;
  }

  NetworkAddress NetworkAddress::from_json(const json::Object& o)
  {
    NetworkAddress addr;
    auto it = o.find("address");
    if (it != o.end() && it->second.is_str())
      addr.address = it->second.as_str();
    it = o.find("family");
    if (it != o.end() && it->second.is_str()) {
      auto fam = it->second.as_str();
      if (fam == "ipv4")
        addr.family = ADDR_FAMILY_IPV4;
      else if (fam == "ipv6")
        addr.family = ADDR_FAMILY_IPV6;
      else
        addr.family = ADDR_FAMILY_UNSPEC;
    }
    it = o.find("scope_id");
    if (it != o.end() && it->second.is_num())
      addr.scope_id = (int)it->second.as_num();
    return addr;
  }

  json::Object RendezvousConfig::to_json() const
  {
    json::Object o;
    o["url"] = url;
    o["heartbeat_sec"] = (double)heartbeat_sec;
    if (!domain_uuid.empty())
      o["domain_uuid"] = domain_uuid;
    if (!domain_nickname.empty())
      o["domain_nickname"] = domain_nickname;
    // crypto_key is intentionally NOT serialized to disk for security.
    return o;
  }

  RendezvousConfig RendezvousConfig::from_json(const json::Object& o)
  {
    RendezvousConfig rc;
    auto it = o.find("url");
    if (it != o.end() && it->second.is_str())
      rc.url = it->second.as_str();
    it = o.find("crypto_key");
    if (it != o.end() && it->second.is_str())
      rc.crypto_key = it->second.as_str();
    it = o.find("domain_uuid");
    if (it != o.end() && it->second.is_str())
      rc.domain_uuid = it->second.as_str();
    it = o.find("domain_nickname");
    if (it != o.end() && it->second.is_str())
      rc.domain_nickname = it->second.as_str();
    it = o.find("heartbeat_sec");
    if (it != o.end() && it->second.is_num())
      rc.heartbeat_sec = (int)it->second.as_num();
    return rc;
  }

  json::Object NetworkConfig::to_json() const
  {
    json::Object o;
    o["config_version"] = (double)CONFIG_VERSION;
    o["identity"] = identity.to_json();
    json::Array bind_arr;
    for (auto& b : bind)
      bind_arr.push_back(b.to_json());
    o["bind"] = bind_arr;
    o["port"] = (double)port;
    o["discovery_port"] = (double)discovery_port;
    o["http_port"] = (double)http_port;
    o["https_port"] = (double)https_port;
    json::Array rv_arr;
    for (auto& r : rendezvous)
      rv_arr.push_back(r.to_json());
    o["rendezvous"] = rv_arr;
    o["request_timeout_ms"] = (double)request_timeout_ms;
    o["rate_limit_ms"] = (double)rate_limit_ms;
    o["include_hostname"] = include_hostname;
    o["enable_update_check"] = enable_update_check;
    o["update_check_interval_hours"] = (double)update_check_interval_hours;
    o["deployment"] = deployment;
    o["license"] = license_type;
    return o;
  }

  NetworkConfig NetworkConfig::from_json(const json::Object& o)
  {
    NetworkConfig cfg;

    // Check config version for forward compatibility
    auto it_v = o.find("config_version");
    if (it_v != o.end() && it_v->second.is_num()) {
      int file_version = (int)it_v->second.as_num();
      if (file_version > CONFIG_VERSION) {
        LOG_WARN("Config file version " + std::to_string(file_version) +
                 " is newer than supported " + std::to_string(CONFIG_VERSION) +
                 " — some settings may be ignored");
      }
      else if (file_version < CONFIG_VERSION) {
        LOG_DEBUG("Migrating config from v" + std::to_string(file_version) + " to v" +
                  std::to_string(CONFIG_VERSION));
        // Future: add migration logic here per version
      }
    }
    else {
      LOG_DEBUG("Config file has no version field — assuming v1");
    }

    auto it = o.find("identity");
    if (it != o.end() && it->second.is_obj())
      cfg.identity = InstanceIdentity::from_json(it->second.as_obj());

    it = o.find("bind");
    if (it != o.end() && it->second.is_arr()) {
      cfg.bind.clear();
      for (auto& v : it->second.as_arr()) {
        if (v.is_obj())
          cfg.bind.push_back(NetworkAddress::from_json(v.as_obj()));
      }
    }

    // Helpers for config validation
    auto valid_port = [](int p) { return p >= 0 && p <= 65535; };
    auto valid_timeout = [](int t) { return t >= 0; };

    it = o.find("port");
    if (it != o.end() && it->second.is_num()) {
      cfg.port = (int)it->second.as_num();
      if (!valid_port(cfg.port)) {
        LOG_WARN("Config: port out of range: " + std::to_string(cfg.port));
        cfg.port = 1985;
      }
    }
    it = o.find("discovery_port");
    if (it != o.end() && it->second.is_num()) {
      cfg.discovery_port = (int)it->second.as_num();
      if (!valid_port(cfg.discovery_port)) {
        LOG_WARN("Config: discovery_port out of range: " + std::to_string(cfg.discovery_port));
        cfg.discovery_port = 1986;
      }
    }
    it = o.find("http_port");
    if (it != o.end() && it->second.is_num()) {
      cfg.http_port = (int)it->second.as_num();
      if (!valid_port(cfg.http_port)) {
        LOG_WARN("Config: http_port out of range: " + std::to_string(cfg.http_port));
        cfg.http_port = 0;
      }
    }
    it = o.find("https_port");
    if (it != o.end() && it->second.is_num()) {
      cfg.https_port = (int)it->second.as_num();
      if (!valid_port(cfg.https_port)) {
        LOG_WARN("Config: https_port out of range: " + std::to_string(cfg.https_port));
        cfg.https_port = 0;
      }
    }

    it = o.find("rendezvous");
    if (it != o.end() && it->second.is_arr()) {
      for (auto& v : it->second.as_arr()) {
        if (v.is_obj())
          cfg.rendezvous.push_back(RendezvousConfig::from_json(v.as_obj()));
      }
    }

    it = o.find("request_timeout_ms");
    if (it != o.end() && it->second.is_num()) {
      cfg.request_timeout_ms = (int)it->second.as_num();
      if (!valid_timeout(cfg.request_timeout_ms)) {
        LOG_WARN("Config: request_timeout_ms invalid: " + std::to_string(cfg.request_timeout_ms));
        cfg.request_timeout_ms = 5000;
      }
    }
    it = o.find("rate_limit_ms");
    if (it != o.end() && it->second.is_num()) {
      cfg.rate_limit_ms = (int)it->second.as_num();
      if (!valid_timeout(cfg.rate_limit_ms)) {
        LOG_WARN("Config: rate_limit_ms invalid: " + std::to_string(cfg.rate_limit_ms));
        cfg.rate_limit_ms = 0;
      }
    }
    it = o.find("include_hostname");
    if (it != o.end() && it->second.is_bool())
      cfg.include_hostname = it->second.as_bool();
    it = o.find("enable_update_check");
    if (it != o.end() && it->second.is_bool())
      cfg.enable_update_check = it->second.as_bool();
    it = o.find("update_check_interval_hours");
    if (it != o.end() && it->second.is_num())
      cfg.update_check_interval_hours = (int)it->second.as_num();

    it = o.find("deployment");
    if (it != o.end() && it->second.is_str()) {
      cfg.deployment = it->second.as_str();
      // Backward compatibility: v0.4.x used "personal", now "interactive"
      if (cfg.deployment == "personal")
        cfg.deployment = "interactive";
    }

    it = o.find("license");
    if (it != o.end() && it->second.is_str())
      cfg.license_type = it->second.as_str();

    return cfg;
  }

  // ══════════════════════════════════════════════════════════════════════════════
  // Config File I/O
  // ══════════════════════════════════════════════════════════════════════════════

  static void ensure_dir_exists(const std::string& path)
  {
    if (path.empty())
      return;
#ifdef _WIN32
    // Create directory tree recursively (CreateDirectoryA only creates one level)
    std::string p = path;
    // Use forward slashes for consistency
    for (auto& c : p)
      if (c == '/')
        c = '\\';
    // Try to create each component from root
    size_t pos = 0;
    while (true) {
      pos = p.find_first_of("\\", pos + 1);
      if (pos == std::string::npos) {
        CreateDirectoryA(p.c_str(), nullptr);
        break;
      }
      std::string part = p.substr(0, pos);
      if (part.size() > 3) // skip "C:\" root
        CreateDirectoryA(part.c_str(), nullptr);
    }
#else
    // POSIX: mkdir -p equivalent
    std::string p = path;
    for (size_t pos = 1; pos != std::string::npos; pos = p.find('/', pos + 1)) {
      mkdir(p.substr(0, pos).c_str(), 0755);
    }
    mkdir(p.c_str(), 0755);
#endif
  }

  static std::string read_file(const std::string& path)
  {
    std::ifstream f(path);
    if (!f.is_open())
      return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }

  static bool write_file(const std::string& path, const std::string& content)
  {
    // Atomic write: write to .tmp, then rename to target
    // This prevents partial/corrupt files on crash or power loss
    std::string tmp_path = path + ".tmp";
    {
      std::ofstream f(tmp_path);
      if (!f.is_open())
        return false;
      f << content;
      if (!f.good()) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return false;
      }
    }
    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);
    return !ec;
  }

  NetworkConfig load_config(const std::string& path)
  {
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
    }
    catch (const std::exception& e) {
      LOG_ERROR("Failed to parse config file " + path + ": " + e.what());
    }

    // Merge install.json sidecar (written by installer with deployment + license)
    std::string install_json_path = path;
    auto slash = install_json_path.find_last_of("/\\");
    if (slash != std::string::npos)
      install_json_path = install_json_path.substr(0, slash + 1) + "install.json";
    else
      install_json_path = "install.json";
    auto install_content = read_file(install_json_path);
    if (!install_content.empty()) {
      try {
        auto iv = json::parse(install_content);
        if (iv.is_obj()) {
          auto io = iv.as_obj();
          auto it_d = io.find("deployment");
          if (it_d != io.end() && it_d->second.is_str()) {
            cfg.deployment = it_d->second.as_str();
            if (cfg.deployment == "personal")
              cfg.deployment = "interactive";
          }
          auto it_l = io.find("license");
          if (it_l != io.end() && it_l->second.is_str())
            cfg.license_type = it_l->second.as_str();
          LOG_DEBUG("Merged install.json: deployment=" + cfg.deployment +
                    ", license=" + cfg.license_type);

          // Remove install.json after merging (one-time migration)
          std::error_code ec;
          std::filesystem::remove(install_json_path, ec);
          if (ec)
            LOG_DEBUG("Could not remove install.json: " + ec.message());

          // Persist merged values back to config.json
          save_config(path, cfg);
        }
      }
      catch (const std::exception& e) {
        LOG_ERROR("Failed to parse install.json: " + std::string(e.what()));
      }
    }

    return cfg;
  }

  void save_config(const std::string& path, const NetworkConfig& cfg)
  {
    auto dir = path.substr(0, path.find_last_of("/\\"));
    ensure_dir_exists(dir);
    auto content = json::dumps(cfg.to_json());
    if (write_file(path, content)) {
      // Round-trip validation: read back and verify key fields
      auto verify = load_config(path);
      if (verify.port != cfg.port || verify.discovery_port != cfg.discovery_port ||
          verify.identity.uuid != cfg.identity.uuid) {
        LOG_ERROR("Config validation FAILED for " + path + " — write may be corrupt");
      }
      else {
        LOG_DEBUG("Saved config to " + path + " (validated)");
      }
    }
    else {
      LOG_ERROR("Failed to write config to " + path);
    }
  }

  InstanceIdentity load_or_create_identity(const std::string& config_dir)
  {
    ensure_dir_exists(config_dir);
    std::string id_path = config_dir + "/instance.id";
    auto content = read_file(id_path);
    if (!content.empty()) {
      // Trim whitespace
      content.erase(std::remove_if(content.begin(), content.end(),
                                   [](char c) { return c == '\r' || c == '\n' || c == ' '; }),
                    content.end());
      // Validate UUID format
      std::regex uuid_regex(
          "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-4[0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}");
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

    id.name = id.hostname; // default name is hostname
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
