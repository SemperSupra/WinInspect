#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// Secure credential storage for agent passwords, SSH keys, and API tokens.
//
// Backend: Windows Credential Manager (wincred.h) on Windows,
//          DPAPI-encrypted file on Wine.
// Both are available on their respective platforms.

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace wininspect {

  struct CredentialEntry
  {
    std::string target;        // "WinInspect:<system>:<type>:<purpose>"
    std::string username;      // e.g. "agent-bob", "root"
    std::string type;          // "ssh-key", "password", "api-token", "certificate"
    std::vector<uint8_t> blob; // encrypted credential data
  };

  class CredentialManager
  {
  public:
    CredentialManager();

    /// Store a credential. Returns true on success.
    bool store(const std::string& target, const std::string& username, const std::string& type,
               const std::vector<uint8_t>& blob);

    /// Retrieve a credential by target name. Returns nullopt if not found.
    std::optional<CredentialEntry> retrieve(const std::string& target) const;

    /// Delete a credential by target name.
    bool remove(const std::string& target);

    /// Force DPAPI backend (useful for testing, avoids WinCred permission issues).
    void force_dpapi();

    /// List all stored credential targets and metadata (no blob data).
    std::vector<CredentialEntry> list() const;

    /// Generate a cryptographically random password of given length.
    /// Characters: A-Z, a-z, 0-9, and specials.
    static std::string generate_password(size_t length = 32);

    /// Generate an Ed25519 keypair, returns {private_key_pem, public_key_pem}.
    static std::pair<std::string, std::string> generate_ed25519_keypair();

    /// Filter known credential target names from a string (for log redaction).
    /// Replaces target names with "<REDACTED>".
    std::string redact(const std::string& input) const;

  private:
    // Backend selection
    bool use_credential_manager_;
    std::string vault_path_; // path to DPAI-encrypted vault file (Wine fallback)

    // Backend implementations
    bool store_wincred(const CredentialEntry& entry) const;
    std::optional<CredentialEntry> retrieve_wincred(const std::string& target) const;
    bool remove_wincred(const std::string& target) const;
    std::vector<CredentialEntry> list_wincred() const;

    bool store_dpapi(const CredentialEntry& entry) const;
    std::optional<CredentialEntry> retrieve_dpapi(const std::string& target) const;
    bool remove_dpapi(const std::string& target) const;
    std::vector<CredentialEntry> list_dpapi() const;

    // Load/save the DPAPI-encrypted vault file
    std::vector<CredentialEntry> load_vault() const;
    bool save_vault(const std::vector<CredentialEntry>& entries) const;

    // Redaction helpers
    mutable std::vector<std::string> known_targets_;
    void refresh_known_targets() const;
  };

} // namespace wininspect
