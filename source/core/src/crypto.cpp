#include "wininspect/base64.hpp"
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>
#include <cstring>

#include "wininspect/crypto.hpp"
#include "wininspect/logger.hpp"

#include <winternl.h>

#pragma comment(lib, "bcrypt.lib")

namespace wininspect::crypto {

  struct BCryptState
  {
    BCRYPT_ALG_HANDLE hAlgECDH = nullptr;
    BCRYPT_KEY_HANDLE hLocalKey = nullptr;
    BCRYPT_ALG_HANDLE hAlgAES = nullptr;
    BCRYPT_KEY_HANDLE hSessionKey = nullptr;
  };

  CryptoSession::CryptoSession()
  {
    hAlgAES_ = new BCryptState();
  }

  CryptoSession::~CryptoSession()
  {
    BCryptState* st = (BCryptState*)hAlgAES_;
    if (st->hSessionKey)
      BCryptDestroyKey(st->hSessionKey);
    if (st->hAlgAES)
      BCryptCloseAlgorithmProvider(st->hAlgAES, 0);
    if (st->hLocalKey)
      BCryptDestroyKey(st->hLocalKey);
    if (st->hAlgECDH)
      BCryptCloseAlgorithmProvider(st->hAlgECDH, 0);
    delete st;
  }

  std::vector<uint8_t> CryptoSession::generate_local_key()
  {
    BCryptState* st = (BCryptState*)hAlgAES_;
    if (BCryptOpenAlgorithmProvider(&st->hAlgECDH, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0) != 0)
      return {};
    if (BCryptGenerateKeyPair(st->hAlgECDH, &st->hLocalKey, 256, 0) != 0)
      return {};
    if (BCryptFinalizeKeyPair(st->hLocalKey, 0) != 0)
      return {};

    ULONG cbBlob = 0;
    BCryptExportKey(st->hLocalKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &cbBlob, 0);
    std::vector<uint8_t> blob(cbBlob);
    BCryptExportKey(st->hLocalKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob.data(), cbBlob, &cbBlob, 0);
    return blob;
  }

  bool CryptoSession::compute_shared_secret(const std::vector<uint8_t>& remote_pubkey)
  {
    BCryptState* st = (BCryptState*)hAlgAES_;
    BCRYPT_KEY_HANDLE hRemoteKey = nullptr;
    if (BCryptImportKeyPair(st->hAlgECDH, nullptr, BCRYPT_ECCPUBLIC_BLOB, &hRemoteKey,
                            (PUCHAR)remote_pubkey.data(), (ULONG)remote_pubkey.size(), 0) != 0)
      return false;

    BCRYPT_SECRET_HANDLE hSecret = nullptr;
    if (BCryptSecretAgreement(st->hLocalKey, hRemoteKey, &hSecret, 0) != 0) {
      BCryptDestroyKey(hRemoteKey);
      return false;
    }

    BCryptBufferDesc derDesc = {0};
    BCryptBuffer derBuffers[1] = {0};
    derDesc.cBuffers = 1;
    derDesc.pBuffers = derBuffers;
    derDesc.ulVersion = BCRYPTBUFFER_VERSION;
    derBuffers[0].BufferType = KDF_HASH_ALGORITHM;
    derBuffers[0].cbBuffer = (ULONG)((wcslen(BCRYPT_SHA256_ALGORITHM) + 1) * sizeof(wchar_t));
    derBuffers[0].pvBuffer = (PVOID)BCRYPT_SHA256_ALGORITHM;

    uint8_t derived[32];
    ULONG cbDerived = 0;
    if (BCryptDeriveKey(hSecret, BCRYPT_KDF_HASH, &derDesc, derived, 32, &cbDerived, 0) != 0) {
      BCryptDestroySecret(hSecret);
      BCryptDestroyKey(hRemoteKey);
      return false;
    }

    BCryptDestroySecret(hSecret);
    BCryptDestroyKey(hRemoteKey);

    if (BCryptOpenAlgorithmProvider(&st->hAlgAES, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
      return false;
    if (BCryptSetProperty(st->hAlgAES, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                          (ULONG)((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)), 0) != 0)
      return false;

    if (BCryptGenerateSymmetricKey(st->hAlgAES, &st->hSessionKey, nullptr, 0, derived, 32, 0) != 0)
      return false;

    initialized_ = true;
    return true;
  }

  std::vector<uint8_t> CryptoSession::encrypt(const std::string& plaintext)
  {
    if (!initialized_)
      return {};
    BCryptState* st = (BCryptState*)hAlgAES_;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);

    uint8_t nonce[12] = {0};
    memcpy(nonce, &nonce_counter_, sizeof(nonce_counter_));
    nonce_counter_++;

    uint8_t tag[16];
    authInfo.pbNonce = nonce;
    authInfo.cbNonce = 12;
    authInfo.pbTag = tag;
    authInfo.cbTag = 16;

    ULONG cbCipher = 0;
    if (BCryptEncrypt(st->hSessionKey, (PUCHAR)plaintext.data(), (ULONG)plaintext.size(), &authInfo,
                      nullptr, 0, nullptr, 0, &cbCipher, 0) != 0)
      return {};

    std::vector<uint8_t> out(12 + 16 + cbCipher);
    memcpy(out.data(), nonce, 12);

    if (BCryptEncrypt(st->hSessionKey, (PUCHAR)plaintext.data(), (ULONG)plaintext.size(), &authInfo,
                      nullptr, 0, out.data() + 28, cbCipher, &cbCipher, 0) != 0)
      return {};

    memcpy(out.data() + 12, tag, 16);
    return out;
  }

  std::string CryptoSession::decrypt(const std::vector<uint8_t>& ciphertext)
  {
    if (!initialized_ || ciphertext.size() < 28)
      return "";
    BCryptState* st = (BCryptState*)hAlgAES_;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);

    uint8_t nonce[12];
    uint8_t tag[16];
    memcpy(nonce, ciphertext.data(), 12);
    memcpy(tag, ciphertext.data() + 12, 16);

    authInfo.pbNonce = nonce;
    authInfo.cbNonce = 12;
    authInfo.pbTag = tag;
    authInfo.cbTag = 16;

    ULONG cbPlain = 0;
    ULONG cbCipher = (ULONG)ciphertext.size() - 28;
    if (BCryptDecrypt(st->hSessionKey, (PUCHAR)ciphertext.data() + 28, cbCipher, &authInfo, nullptr,
                      0, nullptr, 0, &cbPlain, 0) != 0)
      return "";

    std::string out;
    out.resize(cbPlain);
    if (BCryptDecrypt(st->hSessionKey, (PUCHAR)ciphertext.data() + 28, cbCipher, &authInfo, nullptr,
                      0, (PUCHAR)out.data(), cbPlain, &cbPlain, 0) != 0)
      return "";

    return out;
  }

  static std::vector<uint8_t> parse_ssh_pubkey(const std::string& line)
  {
    std::stringstream ss(line);
    std::string type, b64;
    ss >> type >> b64;
    if (type != "ssh-ed25519")
      return {};
    auto decoded = base64::decode(b64);
    // SSH Ed25519 pubkey format: [len][type][len][pubkey]
    // For Ed25519, the last 32 bytes are the raw key.
    if (decoded.size() < 32)
      return {};
    return std::vector<uint8_t>(decoded.end() - 32, decoded.end());
  }

  bool verify_ssh_sig(const std::vector<uint8_t>& message, const std::string& sig_b64,
                      const std::string& pubkey_line)
  {
    auto raw_pubkey = parse_ssh_pubkey(pubkey_line);
    if (raw_pubkey.empty())
      return false;

    // Decode signature blob.
    // In a full implementation, we'd parse the SSHSIG wrapper.
    // For brevity, we assume the signature is the raw 64-byte Ed25519 signature.
    auto raw_sig = base64::decode(sig_b64);
    if (raw_sig.size() < 64)
      return false;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, L"ECC_ED25519", nullptr, 0) != 0)
      return false;

    BCRYPT_KEY_HANDLE hKey = nullptr;
    // For BCrypt, we need to wrap the raw 32-byte pubkey in a BCRYPT_ECCKEY_BLOB
    std::vector<uint8_t> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 32);
    PBCRYPT_ECCKEY_BLOB pBlob = (PBCRYPT_ECCKEY_BLOB)blob.data();
    pBlob->dwMagic = BCRYPT_ECDSA_PUBLIC_GENERIC_MAGIC;
    // Ed25519 uses the ECDSA variant generic blob format for key import.
    // Algorithm is ECC_ED25519 per the BCryptOpenAlgorithmProvider call above.
    pBlob->cbKey = 32;
    memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), raw_pubkey.data(), 32);

    bool ok = false;
    if (BCryptImportKeyPair(hAlg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &hKey, blob.data(),
                            (ULONG)blob.size(), 0) == 0) {
      if (BCryptVerifySignature(hKey, nullptr, (PUCHAR)message.data(), (ULONG)message.size(),
                                (PUCHAR)raw_sig.data(), 64, 0) == 0) {
        ok = true;
      }
      BCryptDestroyKey(hKey);
    }

    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
  }

  // ── OpenSSL-Based SSH Key Parsing and Signing ─────────────────────────────
  // Uses PEM_read_bio_PrivateKey + EVP_DigestSign for Ed25519 signing.
  // Statically linked — no DLLs needed at runtime.
  // Works on both Windows (MSVC) and Wine (MinGW cross-compile).

#if defined(WININSPECT_HAVE_OPENSSL)
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#endif

  // OpenSSH private key PEM parser and SSHSIG wrapper helpers
  static std::vector<uint8_t> read_file_bytes(const std::string& path)
  {
    std::ifstream f(path, std::ios::binary);
    if (!f)
      return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
  }

  static std::string read_pem(const std::vector<uint8_t>& data)
  {
    std::string s(data.begin(), data.end());
    auto start = s.find("-----BEGIN");
    if (start == std::string::npos)
      return {};
    auto nl1 = s.find('\n', start);
    if (nl1 == std::string::npos)
      return {};
    auto end = s.find("-----END", nl1);
    if (end == std::string::npos)
      return {};
    std::string b64;
    size_t pos = nl1 + 1;
    while (pos < end) {
      auto nl = s.find('\n', pos);
      if (nl == std::string::npos)
        nl = end;
      std::string line = s.substr(pos, nl - pos);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (!line.empty())
        b64 += line;
      pos = nl + 1;
    }
    return b64;
  }

  static std::string wrap_sshsig(const std::vector<uint8_t>& raw_sig)
  {
    auto write_u32 = [](uint8_t* p, uint32_t v) {
      p[0] = (uint8_t)(v >> 24);
      p[1] = (uint8_t)(v >> 16);
      p[2] = (uint8_t)(v >> 8);
      p[3] = (uint8_t)(v & 0xFF);
    };
    std::vector<uint8_t> out;
    auto append_str = [&](const std::string& s) {
      uint8_t hdr[4];
      write_u32(hdr, (uint32_t)s.size());
      out.insert(out.end(), hdr, hdr + 4);
      out.insert(out.end(), s.begin(), s.end());
    };
    const uint8_t magic[] = {'S', 'S', 'H', 'S', 'I', 'G', 0};
    out.insert(out.end(), magic, magic + 7);
    uint8_t ver[4] = {0, 0, 0, 1};
    out.insert(out.end(), ver, ver + 4);
    append_str("ssh-ed25519");
    uint32_t slen = (uint32_t)raw_sig.size();
    uint8_t shdr[4];
    write_u32(shdr, slen);
    out.insert(out.end(), shdr, shdr + 4);
    out.insert(out.end(), raw_sig.begin(), raw_sig.end());
    append_str("file");
    return base64::encode(out);
  }

  std::string sign_ssh_msg(const std::vector<uint8_t>& message, const std::string& private_key_path)
  {
#if defined(WININSPECT_HAVE_OPENSSL)
    // 1. Read key file
    auto file_data = read_file_bytes(private_key_path);
    if (file_data.empty()) {
      LOG_ERROR("sign_ssh_msg: cannot read " + private_key_path);
      return "";
    }

    // 2. Parse with OpenSSL — handles all PEM key formats
    BIO* bio = BIO_new_mem_buf(file_data.data(), (int)file_data.size());
    if (!bio) {
      LOG_ERROR("sign_ssh_msg: BIO_new_mem_buf failed");
      return "";
    }

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!pkey) {
      unsigned long err = ERR_get_error();
      char err_buf[256];
      ERR_error_string_n(err, err_buf, sizeof(err_buf));
      LOG_ERROR("sign_ssh_msg: " + std::string(err_buf) + ". Key must be Ed25519, unencrypted.");
      return "";
    }

    if (EVP_PKEY_id(pkey) != EVP_PKEY_ED25519) {
      LOG_ERROR("sign_ssh_msg: only Ed25519 keys supported");
      EVP_PKEY_free(pkey);
      return "";
    }

    // 3. Sign
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
      EVP_PKEY_free(pkey);
      return "";
    }

    std::string result;
    if (EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, pkey) == 1) {
      size_t sig_len = 64;
      std::vector<uint8_t> sig(sig_len);
      if (EVP_DigestSign(mdctx, sig.data(), &sig_len, message.data(), message.size()) == 1) {
        sig.resize(sig_len);
        result = wrap_sshsig(sig);
      }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return result;
#else
    (void)message;
    (void)private_key_path;
    LOG_ERROR("sign_ssh_msg: OpenSSL not available. "
              "Install OpenSSL development headers and rebuild.");
    return "";
#endif
  }

} // namespace wininspect::crypto
#else
// Non-windows fallback
#include "wininspect/crypto.hpp"
#include <string>
#include <vector>

namespace wininspect::crypto {

  CryptoSession::CryptoSession() {}
  CryptoSession::~CryptoSession() {}
  std::vector<uint8_t> CryptoSession::generate_local_key()
  {
    return {};
  }
  bool CryptoSession::compute_shared_secret(const std::vector<uint8_t>&)
  {
    return false;
  }
  std::vector<uint8_t> CryptoSession::encrypt(const std::string&)
  {
    return {};
  }
  std::string CryptoSession::decrypt(const std::vector<uint8_t>&)
  {
    return "";
  }

  bool verify_ssh_sig(const std::vector<uint8_t>&, const std::string&, const std::string&)
  {
    return false;
  }
  std::string sign_ssh_msg(const std::vector<uint8_t>&, const std::string&)
  {
    return "";
  }
} // namespace wininspect::crypto
#endif