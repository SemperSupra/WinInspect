// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/cert.hpp"
#include "wininspect/base64.hpp"
#include "wininspect/logger.hpp"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

#include <sstream>
#include <chrono>
#include <cstring>

namespace wininspect {

// ── HMAC-SHA256 ─────────────────────────────────────────────────────────────

static std::vector<uint8_t> hmac_sha256(const std::vector<uint8_t> &key,
                                          const std::vector<uint8_t> &data) {
#ifdef _WIN32
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr,
                               BCRYPT_ALG_HANDLE_HMAC_FLAG);
  DWORD hash_len = 0;
  DWORD result_len = 0;
  BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len,
                    sizeof(hash_len), &result_len, 0);
  std::vector<uint8_t> hmac(hash_len);
  BCryptHash(hAlg, (PUCHAR)key.data(), (ULONG)key.size(),
             (PUCHAR)data.data(), (ULONG)data.size(),
             hmac.data(), (ULONG)hmac.size());
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return hmac;
#else
  (void)key; (void)data; return {};
#endif
}

// ── SHA-256 (for CA fingerprint) ────────────────────────────────────────────

static std::vector<uint8_t> sha256(const std::vector<uint8_t> &data) {
#ifdef _WIN32
  BCRYPT_ALG_HANDLE hAlg = nullptr;
  BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
  DWORD hash_len = 0, result_len = 0;
  BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len,
                    sizeof(hash_len), &result_len, 0);
  std::vector<uint8_t> hash(hash_len);
  BCryptHash(hAlg, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(),
             hash.data(), (ULONG)hash.size());
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return hash;
#else
  (void)data; return {};
#endif
}

// ── Certificate Fields String ───────────────────────────────────────────────

std::string cert_fields_to_string(const Certificate &cert) {
  std::ostringstream ss;
  ss << cert.uuid << "|"
     << cert.instance_name << "|"
     << cert.hostname << "|"
     << cert.public_key << "|"
     << cert.ca_fingerprint << "|"
     << cert.issued_at << "|"
     << cert.expires_at;
  return ss.str();
}

// ── Generate CA Key ─────────────────────────────────────────────────────────

std::string generate_ca_key() {
  uint8_t key[32];
#ifdef _WIN32
  if (BCryptGenRandom(nullptr, key, sizeof(key),
                       BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
    LOG_ERROR("Failed to generate CA key");
    return "";
  }
#else
  std::memset(key, 0, sizeof(key));
#endif
  return base64::encode(std::vector<uint8_t>(key, key + sizeof(key)));
}

// ── Sign Certificate ────────────────────────────────────────────────────────

Certificate sign_certificate(const std::string &ca_key,
                              const std::string &uuid,
                              const std::string &instance_name,
                              const std::string &hostname,
                              const std::string &ecdh_pubkey,
                              int ttl_days) {
  Certificate cert;
  cert.uuid = uuid;
  cert.instance_name = instance_name;
  cert.hostname = hostname;
  cert.public_key = ecdh_pubkey;

  auto now = std::chrono::system_clock::now();
  cert.issued_at = std::chrono::duration_cast<std::chrono::seconds>(
      now.time_since_epoch()).count();
  cert.expires_at = cert.issued_at + ttl_days * 86400;

  // CA fingerprint = SHA256 of CA key
  auto ca_key_raw = base64::decode(ca_key);
  auto fp = sha256(ca_key_raw);
  cert.ca_fingerprint = base64::encode(fp);

  // Sign the canonical fields string
  auto fields = cert_fields_to_string(cert);
  auto sig = hmac_sha256(ca_key_raw,
                          std::vector<uint8_t>(fields.begin(), fields.end()));
  cert.signature = base64::encode(sig);

  return cert;
}

// ── Verify Certificate ──────────────────────────────────────────────────────

bool verify_certificate(const Certificate &cert, const std::string &ca_key) {
  // Check expiry
  auto now = std::chrono::system_clock::now();
  auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
      now.time_since_epoch()).count();
  if (now_ts > cert.expires_at) {
    LOG_WARN("Certificate expired for " + cert.instance_name);
    return false;
  }

  // Verify CA fingerprint
  auto ca_key_raw = base64::decode(ca_key);
  auto fp = sha256(ca_key_raw);
  auto expected_fp = base64::encode(fp);
  if (cert.ca_fingerprint != expected_fp) {
    LOG_WARN("CA fingerprint mismatch for " + cert.instance_name);
    return false;
  }

  // Verify signature
  auto fields = cert_fields_to_string(cert);
  auto expected_sig = hmac_sha256(ca_key_raw,
                                    std::vector<uint8_t>(fields.begin(),
                                                         fields.end()));
  auto expected_sig_b64 = base64::encode(expected_sig);
  if (cert.signature != expected_sig_b64) {
    LOG_WARN("Certificate signature invalid for " + cert.instance_name);
    return false;
  }

  return true;
}

// ── JSON Serialization ──────────────────────────────────────────────────────

json::Object Certificate::to_json() const {
  json::Object o;
  o["uuid"] = uuid;
  o["instance_name"] = instance_name;
  o["hostname"] = hostname;
  o["public_key"] = public_key;
  o["ca_fingerprint"] = ca_fingerprint;
  o["issued_at"] = (double)issued_at;
  o["expires_at"] = (double)expires_at;
  o["signature"] = signature;
  return o;
}

Certificate Certificate::from_json(const json::Object &o) {
  Certificate cert;
  auto it = o.find("uuid"); if (it != o.end() && it->second.is_str()) cert.uuid = it->second.as_str();
  it = o.find("instance_name"); if (it != o.end() && it->second.is_str()) cert.instance_name = it->second.as_str();
  it = o.find("hostname"); if (it != o.end() && it->second.is_str()) cert.hostname = it->second.as_str();
  it = o.find("public_key"); if (it != o.end() && it->second.is_str()) cert.public_key = it->second.as_str();
  it = o.find("ca_fingerprint"); if (it != o.end() && it->second.is_str()) cert.ca_fingerprint = it->second.as_str();
  it = o.find("issued_at"); if (it != o.end() && it->second.is_num()) cert.issued_at = (int64_t)it->second.as_num();
  it = o.find("expires_at"); if (it != o.end() && it->second.is_num()) cert.expires_at = (int64_t)it->second.as_num();
  it = o.find("signature"); if (it != o.end() && it->second.is_str()) cert.signature = it->second.as_str();
  return cert;
}

} // namespace wininspect
