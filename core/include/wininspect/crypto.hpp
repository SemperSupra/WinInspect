#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wininspect::crypto {

struct Signature {
  std::string identity;
  std::vector<uint8_t> blob;
};

// Key exchange and session state
class CryptoSession {
public:
  CryptoSession();
  ~CryptoSession();

  // Generates a local X25519 key pair and returns the public key
  std::vector<uint8_t> generate_local_key();

  // Computes the shared secret and initializes symmetric encryption
  bool compute_shared_secret(const std::vector<uint8_t> &remote_pubkey);

  // Encrypts a message using AES-256-GCM
  std::vector<uint8_t> encrypt(const std::string &plaintext);

  // Decrypts a message using AES-256-GCM
  std::string decrypt(const std::vector<uint8_t> &ciphertext);

  bool is_initialized() const { return initialized_; }

private:
  bool initialized_ = false;
  void *hAlgAES_ = nullptr;
  void *hKeyAES_ = nullptr;
  uint64_t nonce_counter_ = 0;
  std::vector<uint8_t> shared_secret_;
};

// Verifies an Ed25519 SSH signature against an authorized_keys-style entry
bool verify_ssh_sig(const std::vector<uint8_t> &message,
                    const std::string &signature_b64,
                    const std::string &public_key_line);

// Signs a message using an Ed25519 private key (OpenSSH format)
std::string sign_ssh_msg(const std::vector<uint8_t> &message,
                         const std::string &private_key_path);

} // namespace wininspect::crypto
