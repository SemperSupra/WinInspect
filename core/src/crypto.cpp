#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#ifndef BCRYPT_ECD_PUBLIC_GENERIC_MAGIC
#define BCRYPT_ECD_PUBLIC_GENERIC_MAGIC 0x50434345
#endif
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>

#include "wininspect/crypto.hpp"

#include <winternl.h>

#pragma comment(lib, "bcrypt.lib")

namespace wininspect::crypto {

static std::vector<uint8_t> base64_decode(const std::string& in);

struct BCryptState {
    BCRYPT_ALG_HANDLE hAlgECDH = nullptr;
    BCRYPT_KEY_HANDLE hLocalKey = nullptr;
    BCRYPT_ALG_HANDLE hAlgAES = nullptr;
    BCRYPT_KEY_HANDLE hSessionKey = nullptr;
};

CryptoSession::CryptoSession() {
    hAlgAES_ = new BCryptState();
}

CryptoSession::~CryptoSession() {
    BCryptState* st = (BCryptState*)hAlgAES_;
    if (st->hSessionKey) BCryptDestroyKey(st->hSessionKey);
    if (st->hAlgAES) BCryptCloseAlgorithmProvider(st->hAlgAES, 0);
    if (st->hLocalKey) BCryptDestroyKey(st->hLocalKey);
    if (st->hAlgECDH) BCryptCloseAlgorithmProvider(st->hAlgECDH, 0);
    delete st;
}

std::vector<uint8_t> CryptoSession::generate_local_key() {
    BCryptState* st = (BCryptState*)hAlgAES_;
    if (BCryptOpenAlgorithmProvider(&st->hAlgECDH, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0) != 0) return {};
    if (BCryptGenerateKeyPair(st->hAlgECDH, &st->hLocalKey, 256, 0) != 0) return {};
    if (BCryptFinalizeKeyPair(st->hLocalKey, 0) != 0) return {};

    ULONG cbBlob = 0;
    BCryptExportKey(st->hLocalKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &cbBlob, 0);
    std::vector<uint8_t> blob(cbBlob);
    BCryptExportKey(st->hLocalKey, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob.data(), cbBlob, &cbBlob, 0);
    return blob;
}

bool CryptoSession::compute_shared_secret(const std::vector<uint8_t>& remote_pubkey) {
    BCryptState* st = (BCryptState*)hAlgAES_;
    BCRYPT_KEY_HANDLE hRemoteKey = nullptr;
    if (BCryptImportKeyPair(st->hAlgECDH, nullptr, BCRYPT_ECCPUBLIC_BLOB, &hRemoteKey, (PUCHAR)remote_pubkey.data(), (ULONG)remote_pubkey.size(), 0) != 0) return false;

    BCRYPT_SECRET_HANDLE hSecret = nullptr;
    if (BCryptSecretAgreement(st->hLocalKey, hRemoteKey, &hSecret, 0) != 0) {
        BCryptDestroyKey(hRemoteKey);
        return false;
    }

    BCryptBufferDesc derDesc = { 0 };
    BCryptBuffer derBuffers[1] = { 0 };
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

    if (BCryptOpenAlgorithmProvider(&st->hAlgAES, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return false;
    if (BCryptSetProperty(st->hAlgAES, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0) return false;

    if (BCryptGenerateSymmetricKey(st->hAlgAES, &st->hSessionKey, nullptr, 0, derived, 32, 0) != 0) return false;

    initialized_ = true;
    return true;
}

std::vector<uint8_t> CryptoSession::encrypt(const std::string& plaintext) {
    if (!initialized_) return {};
    BCryptState* st = (BCryptState*)hAlgAES_;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);

    uint8_t nonce[12] = { 0 };
    memcpy(nonce, &nonce_counter_, sizeof(nonce_counter_));
    nonce_counter_++;

    uint8_t tag[16];
    authInfo.pbNonce = nonce;
    authInfo.cbNonce = 12;
    authInfo.pbTag = tag;
    authInfo.cbTag = 16;

    ULONG cbCipher = 0;
    BCryptEncrypt(st->hSessionKey, (PUCHAR)plaintext.data(), (ULONG)plaintext.size(), &authInfo, nullptr, 0, nullptr, 0, &cbCipher, 0);
    
    std::vector<uint8_t> out(12 + 16 + cbCipher);
    memcpy(out.data(), nonce, 12);
    memcpy(out.data() + 12, tag, 16);
    
    BCryptEncrypt(st->hSessionKey, (PUCHAR)plaintext.data(), (ULONG)plaintext.size(), &authInfo, nullptr, 0, out.data() + 28, cbCipher, &cbCipher, 0);
    return out;
}

std::string CryptoSession::decrypt(const std::vector<uint8_t>& ciphertext) {
    if (!initialized_ || ciphertext.size() < 28) return "";
    BCryptState* st = (BCryptState*)hAlgAES_;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);

    authInfo.pbNonce = (PUCHAR)ciphertext.data();
    authInfo.cbNonce = 12;
    authInfo.pbTag = (PUCHAR)ciphertext.data() + 12;
    authInfo.cbTag = 16;

    ULONG cbPlain = 0;
    ULONG cbCipher = (ULONG)ciphertext.size() - 28;
    if (BCryptDecrypt(st->hSessionKey, (PUCHAR)ciphertext.data() + 28, cbCipher, &authInfo, nullptr, 0, nullptr, 0, &cbPlain, 0) != 0) return "";

    std::string out;
    out.resize(cbPlain);
    if (BCryptDecrypt(st->hSessionKey, (PUCHAR)ciphertext.data() + 28, cbCipher, &authInfo, nullptr, 0, (PUCHAR)out.data(), cbPlain, &cbPlain, 0) != 0) return "";
    
    return out;
}

static std::vector<uint8_t> base64_decode(const std::string& in) {
    static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[b64[i]] = i;
    int val = 0, valb = -8;
    for (char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(uint8_t((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::vector<uint8_t> parse_ssh_pubkey(const std::string& line) {
    std::stringstream ss(line);
    std::string type, b64;
    ss >> type >> b64;
    if (type != "ssh-ed25519") return {};
    auto decoded = base64_decode(b64);
    // SSH Ed25519 pubkey format: [len][type][len][pubkey]
    // For Ed25519, the last 32 bytes are the raw key.
    if (decoded.size() < 32) return {};
    return std::vector<uint8_t>(decoded.end() - 32, decoded.end());
}

bool verify_ssh_sig(const std::vector<uint8_t>& message, const std::string& sig_b64, const std::string& pubkey_line) {
    auto raw_pubkey = parse_ssh_pubkey(pubkey_line);
    if (raw_pubkey.empty()) return false;

    // Decode signature blob. 
    // In a full implementation, we'd parse the SSHSIG wrapper.
    // For brevity, we assume the signature is the raw 64-byte Ed25519 signature.
    auto raw_sig = base64_decode(sig_b64);
    if (raw_sig.size() < 64) return false;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, L"ECC_ED25519", nullptr, 0) != 0) return false;

    BCRYPT_KEY_HANDLE hKey = nullptr;
    // For BCrypt, we need to wrap the raw 32-byte pubkey in a BCRYPT_ECCKEY_BLOB
    std::vector<uint8_t> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 32);
    PBCRYPT_ECCKEY_BLOB pBlob = (PBCRYPT_ECCKEY_BLOB)blob.data();
    pBlob->dwMagic = BCRYPT_ECD_PUBLIC_GENERIC_MAGIC;
    pBlob->cbKey = 32;
    memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), raw_pubkey.data(), 32);

    bool ok = false;
    if (BCryptImportKeyPair(hAlg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &hKey, blob.data(), (ULONG)blob.size(), 0) == 0) {
        if (BCryptVerifySignature(hKey, nullptr, (PUCHAR)message.data(), (ULONG)message.size(), (PUCHAR)raw_sig.data(), 64, 0) == 0) {
            ok = true;
        }
        BCryptDestroyKey(hKey);
    }

    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

std::string sign_ssh_msg(const std::vector<uint8_t>& message, const std::string& private_key_path) {
    // Reading OpenSSH private keys requires a specialized parser (PEM/Base64 + KDF).
    // In a proven production environment, you would use a library like 'libssh2' or 'mbedtls' 
    // to parse the key file. 
    // 
    // Since we must avoid external binaries and complex dependencies, we focus on 
    // the system-provided BCrypt logic for the actual signing operation.
    return "SSHSIG_STUB_REPLACE_WITH_REAL_SIGNING";
}

} // namespace wininspect::crypto
#else
// Non-windows fallback
#include "wininspect/crypto.hpp"
#include <vector>
#include <string>

namespace wininspect::crypto {

CryptoSession::CryptoSession() {}
CryptoSession::~CryptoSession() {}
std::vector<uint8_t> CryptoSession::generate_local_key() { return {}; }
bool CryptoSession::compute_shared_secret(const std::vector<uint8_t>&) { return false; }
std::vector<uint8_t> CryptoSession::encrypt(const std::string&) { return {}; }
std::string CryptoSession::decrypt(const std::vector<uint8_t>&) { return ""; }

bool verify_ssh_sig(const std::vector<uint8_t>&, const std::string&, const std::string&) { return false; }
std::string sign_ssh_msg(const std::vector<uint8_t>&, const std::string&) { return ""; }
}
#endif