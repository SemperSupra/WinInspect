// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/credential_manager.hpp"
#include "wininspect/logger.hpp"
#include "wininspect/base64.hpp"
#include "wininspect/network_config.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstring>
#include <filesystem>
#include <system_error>
#ifdef _WIN32
#include <direct.h> // _mkdir
#endif

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <wincred.h>
// MinGW handles libraries via CMake target_link_libraries, not pragma
#ifndef __MINGW32__
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#endif
#endif

namespace wininspect {

  // ── Naming convention prefix ──────────────────────────────────────────────
  static constexpr const wchar_t* PREFIX = L"WinInspect:";
  static constexpr const char* PREFIX_A = "WinInspect:";

  // ── Constructor: detect backend ───────────────────────────────────────────

  CredentialManager::CredentialManager()
  {
    // Try Windows Credential Manager first
    use_credential_manager_ = false;
#if defined(_WIN32)
    // Test if CredWrite is functional by trying to write a temp entry
    CREDENTIALW test = {};
    test.Type = CRED_TYPE_GENERIC;
    std::wstring test_target = std::wstring(PREFIX) + L"__probe__";
    test.TargetName = const_cast<wchar_t*>(test_target.c_str());
    test.CredentialBlobSize = 1;
    uint8_t dummy = 0;
    test.CredentialBlob = &dummy;
    test.Persist = CRED_PERSIST_SESSION;
    if (CredWriteW(&test, 0)) {
      use_credential_manager_ = true;
      CredDeleteW(test_target.c_str(), CRED_TYPE_GENERIC, 0);
    }
#endif

    // Fallback: DPAPI-encrypted file in config directory
    // Works on both Windows and Wine (APPDATA maps correctly in Wine)
    vault_path_ = default_config_dir() + "/credentials.vault";
    // Ensure the config directory exists (for vault file I/O)
    std::string dir = default_config_dir();
    if (!dir.empty()) {
#ifdef _WIN32
      _mkdir(dir.c_str()); // exists ok, fails silently
#else
      mkdir(dir.c_str(), 0700);
#endif
    }
    refresh_known_targets();
  }

  void CredentialManager::force_dpapi()
  {
    use_credential_manager_ = false;
    vault_path_ = default_config_dir() + "/credentials.vault";
    // Ensure the vault directory exists
    auto dir = default_config_dir();
    if (!dir.empty()) {
#ifdef _WIN32
      _mkdir(dir.c_str());
#else
      mkdir(dir.c_str(), 0700);
#endif
    }
  }

  // ── Backend Detection ─────────────────────────────────────────────────────

  // On Windows: use Credential Manager. On Wine where it may not be available,
  // fall back to DPAPI-encrypted vault file. Both work on their respective
  // platforms.

  // ── Store ─────────────────────────────────────────────────────────────────

  bool CredentialManager::store(const std::string& target, const std::string& username,
                                const std::string& type, const std::vector<uint8_t>& blob)
  {
    CredentialEntry entry{target, username, type, blob};
    bool ok = false;
    if (use_credential_manager_) {
      ok = store_wincred(entry);
    }
    else {
      ok = store_dpapi(entry);
    }
    if (ok)
      refresh_known_targets();
    return ok;
  }

  // ── Retrieve ──────────────────────────────────────────────────────────────

  std::optional<CredentialEntry> CredentialManager::retrieve(const std::string& target) const
  {
    if (use_credential_manager_) {
      return retrieve_wincred(target);
    }
    return retrieve_dpapi(target);
  }

  // ── Remove ────────────────────────────────────────────────────────────────

  bool CredentialManager::remove(const std::string& target)
  {
    bool ok = false;
    if (use_credential_manager_) {
      ok = remove_wincred(target);
    }
    else {
      ok = remove_dpapi(target);
    }
    if (ok)
      refresh_known_targets();
    return ok;
  }

  // ── List ──────────────────────────────────────────────────────────────────

  std::vector<CredentialEntry> CredentialManager::list() const
  {
    if (use_credential_manager_) {
      return list_wincred();
    }
    return list_dpapi();
  }

  // ── WinCred Backend ───────────────────────────────────────────────────────

  bool CredentialManager::store_wincred(const CredentialEntry& entry) const
  {
#if defined(_WIN32)
    std::wstring target_w(entry.target.begin(), entry.target.end());
    std::wstring target_full = std::wstring(PREFIX) + target_w;
    std::wstring username_w(entry.username.begin(), entry.username.end());
    std::wstring type_w(entry.type.begin(), entry.type.end());
    std::wstring comment = L"WinInspect credential (" + type_w + L")";

    CREDENTIALW cred = {};
    cred.Flags = 0;
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t*>(target_full.c_str());
    cred.Comment = const_cast<wchar_t*>(comment.c_str());
    cred.CredentialBlobSize = (DWORD)entry.blob.size();
    cred.CredentialBlob = const_cast<LPBYTE>(entry.blob.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<wchar_t*>(username_w.c_str());
    cred.AttributeCount = 0;
    cred.Attributes = nullptr;

    if (CredWriteW(&cred, 0))
      return true;
    LOG_DEBUG("CredentialManager: CredWrite failed for " + entry.target);
    return false;
#else
    (void)entry;
    return false;
#endif
  }

  std::optional<CredentialEntry>
  CredentialManager::retrieve_wincred(const std::string& target) const
  {
#if defined(_WIN32)
    std::wstring target_full = std::wstring(PREFIX) + std::wstring(target.begin(), target.end());
    PCREDENTIALW pCred = nullptr;
    if (!CredReadW(target_full.c_str(), CRED_TYPE_GENERIC, 0, &pCred)) {
      return std::nullopt;
    }
    CredentialEntry entry;
    entry.target = target;
    if (pCred->UserName) {
      std::wstring ws(pCred->UserName);
      entry.username = std::string(ws.begin(), ws.end());
    }
    entry.blob.resize(pCred->CredentialBlobSize);
    if (pCred->CredentialBlobSize > 0) {
      memcpy(entry.blob.data(), pCred->CredentialBlob, pCred->CredentialBlobSize);
    }
    entry.type = "generic"; // WinCred doesn't store custom type — infer from target
    CredFree(pCred);
    return entry;
#else
    (void)target;
    return std::nullopt;
#endif
  }

  bool CredentialManager::remove_wincred(const std::string& target) const
  {
#if defined(_WIN32)
    std::wstring target_full = std::wstring(PREFIX) + std::wstring(target.begin(), target.end());
    return CredDeleteW(target_full.c_str(), CRED_TYPE_GENERIC, 0) == TRUE;
#else
    (void)target;
    return false;
#endif
  }

  std::vector<CredentialEntry> CredentialManager::list_wincred() const
  {
    std::vector<CredentialEntry> results;
#if defined(_WIN32)
    PCREDENTIALW* pCreds = nullptr;
    DWORD count = 0;
    if (!CredEnumerateW(PREFIX, 0, &count, &pCreds)) {
      return results;
    }
    for (DWORD i = 0; i < count; i++) {
      if (!pCreds[i] || !pCreds[i]->TargetName)
        continue;
      std::wstring target_full(pCreds[i]->TargetName);
      // Strip the "WinInspect:" prefix
      std::string target_a(target_full.begin(), target_full.end());
      size_t pos = target_a.find(':');
      if (pos != std::string::npos)
        target_a = target_a.substr(pos + 1);
      if (target_a.empty())
        continue;
      CredentialEntry entry;
      entry.target = target_a;
      entry.blob = {}; // No blob in list results
      if (pCreds[i]->UserName) {
        std::wstring ws(pCreds[i]->UserName);
        entry.username = std::string(ws.begin(), ws.end());
      }
      results.push_back(std::move(entry));
    }
    CredFree(pCreds);
#endif
    return results;
  }

  // ── DPAPI Vault Backend (Wine-compatible) ─────────────────────────────────

  // The DPAPI vault stores credentials in a JSON-like binary format encrypted
  // with CryptProtectData. Each credential is a separate entry.
  //
  // Vault format (encrypted):
  //   [4-byte entry count N]
  //   For each entry:
  //     [4-byte target_len][target bytes]
  //     [4-byte username_len][username bytes]
  //     [4-byte type_len][type bytes]
  //     [4-byte blob_len][blob bytes]

  static std::vector<uint8_t> dpapi_encrypt(const std::vector<uint8_t>& plaintext)
  {
#if defined(_WIN32)
    DATA_BLOB in = {(DWORD)plaintext.size(), const_cast<BYTE*>(plaintext.data())};
    DATA_BLOB out = {};
    if (CryptProtectData(&in, L"WinInspect Credential Vault", nullptr, nullptr, nullptr, 0, &out)) {
      std::vector<uint8_t> result(out.cbData);
      memcpy(result.data(), out.pbData, out.cbData);
      LocalFree(out.pbData);
      return result;
    }
#endif
    return {};
  }

  static std::vector<uint8_t> dpapi_decrypt(const std::vector<uint8_t>& ciphertext)
  {
#if defined(_WIN32)
    DATA_BLOB in = {(DWORD)ciphertext.size(), const_cast<BYTE*>(ciphertext.data())};
    DATA_BLOB out = {};
    if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
      std::vector<uint8_t> result(out.cbData);
      memcpy(result.data(), out.pbData, out.cbData);
      LocalFree(out.pbData);
      return result;
    }
#endif
    return {};
  }

  // Binary vault I/O
  static bool write_vault_file(const std::string& path, const std::vector<uint8_t>& encrypted)
  {
    // Atomic write: .tmp → rename to prevent partial vault on crash
    std::string tmp_path = path + ".tmp";
    {
      std::ofstream f(tmp_path, std::ios::binary);
      if (!f)
        return false;
      f.write((const char*)encrypted.data(), encrypted.size());
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

  static std::vector<uint8_t> read_vault_file(const std::string& path)
  {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
      return {};
    auto size = f.tellg();
    if (size <= 0)
      return {};
    f.seekg(0);
    std::vector<uint8_t> data((size_t)size);
    f.read((char*)data.data(), size);
    return data;
  }

  // Encode entries to binary format
  static std::vector<uint8_t> encode_entries(const std::vector<CredentialEntry>& entries)
  {
    std::vector<uint8_t> buf;
    auto push_u32 = [&](uint32_t v) {
      buf.push_back((uint8_t)(v >> 24));
      buf.push_back((uint8_t)(v >> 16));
      buf.push_back((uint8_t)(v >> 8));
      buf.push_back((uint8_t)(v & 0xFF));
    };
    auto push_str = [&](const std::string& s) {
      push_u32((uint32_t)s.size());
      buf.insert(buf.end(), s.begin(), s.end());
    };
    auto push_blob = [&](const std::vector<uint8_t>& b) {
      push_u32((uint32_t)b.size());
      buf.insert(buf.end(), b.begin(), b.end());
    };

    push_u32((uint32_t)entries.size());
    for (auto& e : entries) {
      push_str(e.target);
      push_str(e.username);
      push_str(e.type);
      push_blob(e.blob);
    }
    return buf;
  }

  // Decode entries from binary format
  static std::vector<CredentialEntry> decode_entries(const std::vector<uint8_t>& buf)
  {
    std::vector<CredentialEntry> entries;
    if (buf.size() < 4)
      return entries;

    size_t pos = 0;
    auto read_u32 = [&]() -> uint32_t {
      if (pos + 4 > buf.size())
        return 0;
      uint32_t v = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos + 1] << 16) |
                   ((uint32_t)buf[pos + 2] << 8) | (uint32_t)buf[pos + 3];
      pos += 4;
      return v;
    };
    auto read_str = [&]() -> std::string {
      uint32_t len = read_u32();
      if (pos + len > buf.size())
        return {};
      std::string s((const char*)&buf[pos], len);
      pos += len;
      return s;
    };
    auto read_blob = [&]() -> std::vector<uint8_t> {
      uint32_t len = read_u32();
      if (pos + len > buf.size())
        return {};
      std::vector<uint8_t> b(&buf[pos], &buf[pos] + len);
      pos += len;
      return b;
    };

    uint32_t count = read_u32();
    for (uint32_t i = 0; i < count; i++) {
      CredentialEntry e;
      e.target = read_str();
      e.username = read_str();
      e.type = read_str();
      e.blob = read_blob();
      if (e.target.empty())
        break;
      entries.push_back(std::move(e));
    }
    return entries;
  }

  std::vector<CredentialEntry> CredentialManager::load_vault() const
  {
    auto encrypted = read_vault_file(vault_path_);
    if (encrypted.empty())
      return {};
    auto decrypted = dpapi_decrypt(encrypted);
    if (decrypted.empty()) {
      LOG_DEBUG("CredentialManager: vault decryption failed (may be empty or corrupt)");
      return {};
    }
    return decode_entries(decrypted);
  }

  bool CredentialManager::save_vault(const std::vector<CredentialEntry>& entries) const
  {
    auto plaintext = encode_entries(entries);
    auto encrypted = dpapi_encrypt(plaintext);
    if (encrypted.empty()) {
      LOG_ERROR("CredentialManager: encryption failed");
      return false;
    }
    return write_vault_file(vault_path_, encrypted);
  }

  bool CredentialManager::store_dpapi(const CredentialEntry& entry) const
  {
    auto entries = load_vault();
    // Replace existing entry with same target
    for (auto& e : entries) {
      if (e.target == entry.target) {
        e = entry;
        return save_vault(entries);
      }
    }
    entries.push_back(entry);
    return save_vault(entries);
  }

  std::optional<CredentialEntry> CredentialManager::retrieve_dpapi(const std::string& target) const
  {
    auto entries = load_vault();
    for (auto& e : entries) {
      if (e.target == target)
        return e;
    }
    return std::nullopt;
  }

  bool CredentialManager::remove_dpapi(const std::string& target) const
  {
    auto entries = load_vault();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
      if (it->target == target) {
        entries.erase(it);
        return save_vault(entries);
      }
    }
    return false;
  }

  std::vector<CredentialEntry> CredentialManager::list_dpapi() const
  {
    auto entries = load_vault();
    // Strip blobs for listing
    for (auto& e : entries)
      e.blob.clear();
    return entries;
  }

  // ── Password Generation ───────────────────────────────────────────────────

  std::string CredentialManager::generate_password(size_t length)
  {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz"
                           "0123456789"
                           "!@#$%^&*()-_=+";
    const size_t charset_size = sizeof(charset) - 1;

    std::string result;
    result.resize(length);
#if defined(_WIN32)
    // Use BCryptGenRandom for cryptographic randomness
    std::vector<uint8_t> random(length * 2);
    if (BCryptGenRandom(nullptr, random.data(), (ULONG)random.size(),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
      // Fallback to rand() if BCrypt fails
      for (size_t i = 0; i < length; i++)
        result[i] = charset[rand() % charset_size];
      return result;
    }
    for (size_t i = 0; i < length; i++) {
      uint16_t idx = (uint16_t)random[i * 2] | ((uint16_t)random[i * 2 + 1] << 8);
      result[i] = charset[idx % charset_size];
    }
#else
    for (size_t i = 0; i < length; i++)
      result[i] = charset[rand() % charset_size];
#endif
    return result;
  }

  // ── Ed25519 Keypair Generation ────────────────────────────────────────────

  std::pair<std::string, std::string> CredentialManager::generate_ed25519_keypair()
  {
    // Use the existing BCrypt Ed25519 infrastructure from crypto.cpp
    // Generate a keypair, export public and private keys
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, L"ECDSA_ED25519", nullptr, 0) != 0) {
      return {};
    }
    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (BCryptGenerateKeyPair(hAlg, &hKey, 256, 0) != 0) {
      BCryptCloseAlgorithmProvider(hAlg, 0);
      return {};
    }
    if (BCryptFinalizeKeyPair(hKey, 0) != 0) {
      BCryptDestroyKey(hKey);
      BCryptCloseAlgorithmProvider(hAlg, 0);
      return {};
    }

    // Export public key
    ULONG pub_size = 0;
    BCryptExportKey(hKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &pub_size, 0);
    std::vector<uint8_t> pub_blob(pub_size);
    BCryptExportKey(hKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, pub_blob.data(), pub_size, &pub_size, 0);

    // Export private key
    ULONG priv_size = 0;
    BCryptExportKey(hKey, nullptr, BCRYPT_ECCPRIVATE_BLOB, nullptr, 0, &priv_size, 0);
    std::vector<uint8_t> priv_blob(priv_size);
    BCryptExportKey(hKey, nullptr, BCRYPT_ECCPRIVATE_BLOB, priv_blob.data(), priv_size, &priv_size,
                    0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    std::string pub_b64 = base64::encode(pub_blob);
    std::string priv_b64 = base64::encode(priv_blob);
    return {priv_b64, pub_b64};
#else
    return {};
#endif
  }

  // ── Redaction ─────────────────────────────────────────────────────────────

  void CredentialManager::refresh_known_targets() const
  {
    known_targets_.clear();
    auto entries = list();
    for (auto& e : entries) {
      known_targets_.push_back(e.target);
    }
  }

  std::string CredentialManager::redact(const std::string& input) const
  {
    std::string result = input;
    for (auto& target : known_targets_) {
      size_t pos = 0;
      while ((pos = result.find(target, pos)) != std::string::npos) {
        result.replace(pos, target.size(), "<REDACTED>");
        pos += 11; // length of "<REDACTED>"
      }
    }
    return result;
  }

} // namespace wininspect
