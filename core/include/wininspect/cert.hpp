#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "tinyjson.hpp"
#include <string>
#include <vector>

namespace wininspect {

/// Self-signed certificate for daemon identity verification.
/// Uses ECDH key material and HMAC-SHA256 for signatures.
struct Certificate {
  std::string uuid;           // daemon instance UUID
  std::string instance_name;  // daemon name
  std::string hostname;       // OS hostname
  std::string public_key;     // base64 ECDH public key
  std::string ca_fingerprint; // SHA256 of CA public key
  int64_t issued_at{};        // unix timestamp
  int64_t expires_at{};       // unix timestamp
  std::string signature;      // base64 HMAC-SHA256 signature

  json::Object to_json() const;
  static Certificate from_json(const json::Object &o);
};

/// Generate a CA keypair and return the base64-encoded public key.
/// The CA key is a random 32-byte HMAC key.
std::string generate_ca_key();

/// Create a self-signed certificate for a daemon instance.
/// signed with the CA key (HMAC-SHA256 of the cert fields).
Certificate sign_certificate(const std::string &ca_key,
                              const std::string &uuid,
                              const std::string &instance_name,
                              const std::string &hostname,
                              const std::string &ecdh_pubkey,
                              int ttl_days = 365);

/// Verify a certificate's signature against a CA key.
bool verify_certificate(const Certificate &cert, const std::string &ca_key);

/// Serialize certificate fields to canonical string for signing.
std::string cert_fields_to_string(const Certificate &cert);

} // namespace wininspect
