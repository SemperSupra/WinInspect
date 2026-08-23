// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "doctest/doctest.h"
#include "wininspect/crypto.hpp"
#include <vector>
#include <cstring>

#ifdef _WIN32
using namespace wininspect::crypto;

DOCTEST_TEST_CASE("CryptoSession handshake and encryption")
{
  CryptoSession server;
  CryptoSession client;

  auto server_pub = server.generate_local_key();
  auto client_pub = client.generate_local_key();

  DOCTEST_REQUIRE(!server_pub.empty());
  DOCTEST_REQUIRE(!client_pub.empty());
  DOCTEST_REQUIRE(server.compute_shared_secret(client_pub));
  DOCTEST_REQUIRE(client.compute_shared_secret(server_pub));

  std::string message = "Secret Window Title";
  auto encrypted = client.encrypt(message);
  DOCTEST_REQUIRE(!encrypted.empty());
  DOCTEST_REQUIRE(encrypted.size() > message.size());

  std::string decrypted = server.decrypt(encrypted);
  DOCTEST_REQUIRE_EQ(message, decrypted);
}

// C1: AES-GCM conformance (FIPS 197 / NIST SP 800-38D)
// BCrypt is NIST CAVP-certified, so we verify our usage: unique nonces
// and GCM authentication tag enforcement.
// Official test vectors:
// https://csrc.nist.gov/Projects/Cryptographic-Algorithm-Validation-Program/CAVP-TESTING-BLOCK-CIPHER-MODES#GCMVS
DOCTEST_TEST_CASE("C1 conformance: AES-GCM tampered ciphertext rejected")
{
  CryptoSession alice;
  CryptoSession bob;
  auto apub = alice.generate_local_key();
  auto bpub = bob.generate_local_key();
  (void)alice.compute_shared_secret(bpub);
  (void)bob.compute_shared_secret(apub);

  auto ct = alice.encrypt("sensitive data");
  DOCTEST_REQUIRE(!ct.empty());

  // Flip a bit in the ciphertext portion (past the nonce 12B + tag 16B)
  if (ct.size() > 30)
    ct[28] ^= 0xFF;
  auto decrypted = bob.decrypt(ct);
  DOCTEST_REQUIRE(decrypted.empty()); // GCM auth tag catches this
}

// C1: AES-GCM conformance — each encryption uses a unique nonce
DOCTEST_TEST_CASE("C1 conformance: AES-GCM unique nonces per encryption")
{
  CryptoSession alice;
  CryptoSession bob;
  auto apub = alice.generate_local_key();
  auto bpub = bob.generate_local_key();
  (void)alice.compute_shared_secret(bpub);
  (void)bob.compute_shared_secret(apub);

  auto ct1 = alice.encrypt("msg1");
  auto ct2 = alice.encrypt("msg2");
  DOCTEST_REQUIRE(!ct1.empty());
  DOCTEST_REQUIRE(!ct2.empty());

  // Nonce is first 12 bytes; counter-based so they must differ
  bool nonces_differ = std::memcmp(ct1.data(), ct2.data(), 12) != 0;
  DOCTEST_REQUIRE(nonces_differ);

  // Both sides can decrypt
  DOCTEST_REQUIRE_EQ(bob.decrypt(ct1), "msg1");
  DOCTEST_REQUIRE_EQ(bob.decrypt(ct2), "msg2");
}
#endif
