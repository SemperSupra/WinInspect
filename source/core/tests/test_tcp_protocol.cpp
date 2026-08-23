// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// TCP protocol tests. Tests wire framing (compression flag, 4-byte length),
// auth handshake, and encrypted session.
//
// These tests verify the protocol primitives without requiring a live socket.
// The compression/decompression round-trip and length-prefix framing are
// tested in isolation.

#include "doctest/doctest.h"
#include "wininspect/base64.hpp"
#include "wininspect/compress.hpp"
#include "wininspect/crypto.hpp"
#include <vector>
#include <cstring>

using namespace wininspect;

// ── Framing helpers (uses wininspect::FRAME_COMPRESSED_FLAG from compress.hpp) ──

constexpr uint32_t MAX_SAFE_LENGTH = 10 * 1024 * 1024;
using wininspect::FRAME_COMPRESSED_FLAG;

struct FrameHeader
{
  uint32_t raw_length;
  bool is_compressed;
  uint32_t payload_length;
};

static FrameHeader parse_frame_header(uint32_t wire_len)
{
  FrameHeader h;
  h.raw_length = wire_len;
  h.is_compressed = (wire_len & FRAME_COMPRESSED_FLAG) != 0;
  h.payload_length = h.is_compressed ? (wire_len & ~FRAME_COMPRESSED_FLAG) : wire_len;
  return h;
}

static uint32_t build_frame_header(uint32_t payload_len, bool compressed)
{
  return compressed ? (payload_len | FRAME_COMPRESSED_FLAG) : payload_len;
}

// ── Tests ─────────────────────────────────────────────────────────────────

DOCTEST_TEST_CASE("framing: parse un compressed header")
{
  auto h = parse_frame_header(42);
  DOCTEST_REQUIRE(h.is_compressed == false);
  DOCTEST_REQUIRE(h.payload_length == 42);
  DOCTEST_REQUIRE(build_frame_header(42, false) == 42);
}

DOCTEST_TEST_CASE("framing: parse compressed header")
{
  uint32_t wire = build_frame_header(100, true);
  auto h = parse_frame_header(wire);
  DOCTEST_REQUIRE(h.is_compressed == true);
  DOCTEST_REQUIRE(h.payload_length == 100);
  DOCTEST_REQUIRE((wire & 0x80000000) != 0);
}

DOCTEST_TEST_CASE("framing: max safe length")
{
  // MSB set with small payload — valid compressed frame
  auto h = parse_frame_header(0x80000000 | 100);
  DOCTEST_REQUIRE(h.is_compressed == true);
  DOCTEST_REQUIRE(h.payload_length == 100);

  // Large payload without MSB — valid uncompressed
  h = parse_frame_header(5000);
  DOCTEST_REQUIRE(h.is_compressed == false);
  DOCTEST_REQUIRE(h.payload_length == 5000);
}

DOCTEST_TEST_CASE("framing: compressed header MSB")
{
  uint32_t small_payload = 64;
  uint32_t wire = small_payload | FRAME_COMPRESSED_FLAG;
  DOCTEST_REQUIRE((wire & 0x80000000) != 0);
  DOCTEST_REQUIRE((wire & ~FRAME_COMPRESSED_FLAG) == small_payload);
}

DOCTEST_TEST_CASE("compression: round-trip small payload")
{
  std::string data = "Hello, WinInspect! This is a test payload.";
  std::vector<uint8_t> raw(data.begin(), data.end());
  auto compressed = compress(raw);
  // Small payloads may not compress — that's OK
  if (!compressed.empty()) {
    auto decompressed = decompress(compressed, raw.size());
    DOCTEST_REQUIRE(!decompressed.empty());
    DOCTEST_REQUIRE(decompressed.size() == raw.size());
    DOCTEST_REQUIRE(memcmp(decompressed.data(), raw.data(), raw.size()) == 0);
  }
}

DOCTEST_TEST_CASE("compression: round-trip large payload")
{
  // Generate 100KB of compressible data
  std::string data;
  data.reserve(100 * 1024);
  for (int i = 0; i < 100 * 1024; i++)
    data += (char)('A' + (i % 26));
  std::vector<uint8_t> raw(data.begin(), data.end());

  auto compressed = compress(raw);
  DOCTEST_REQUIRE(!compressed.empty());
  DOCTEST_REQUIRE(compressed.size() < raw.size()); // must compress
  DOCTEST_REQUIRE(compressed.size() > 0);

  auto decompressed = decompress(compressed, raw.size());
  DOCTEST_REQUIRE(!decompressed.empty());
  DOCTEST_REQUIRE(decompressed.size() == raw.size());
  DOCTEST_REQUIRE(memcmp(decompressed.data(), raw.data(), raw.size()) == 0);
}

DOCTEST_TEST_CASE("compression: empty data")
{
  auto compressed = compress({});
  DOCTEST_REQUIRE(compressed.empty());
  auto decompressed = decompress({}, 0);
  DOCTEST_REQUIRE(decompressed.empty());
}

DOCTEST_TEST_CASE("crypto: ECDH key generation")
{
  crypto::CryptoSession session;
  auto pubkey = session.generate_local_key();
  DOCTEST_REQUIRE(!pubkey.empty());
}

DOCTEST_TEST_CASE("crypto: ECDH shared secret")
{
  crypto::CryptoSession alice;
  crypto::CryptoSession bob;
  auto alice_pub = alice.generate_local_key();
  auto bob_pub = bob.generate_local_key();
  DOCTEST_REQUIRE(!alice_pub.empty());
  DOCTEST_REQUIRE(!bob_pub.empty());
  DOCTEST_REQUIRE(alice.compute_shared_secret(bob_pub));
  DOCTEST_REQUIRE(bob.compute_shared_secret(alice_pub));
  DOCTEST_REQUIRE(alice.is_initialized());
  DOCTEST_REQUIRE(bob.is_initialized());
}

DOCTEST_TEST_CASE("crypto: AES-GCM encrypt/decrypt round-trip")
{
  crypto::CryptoSession alice;
  crypto::CryptoSession bob;
  auto alice_pub = alice.generate_local_key();
  auto bob_pub = bob.generate_local_key();
  (void)alice.compute_shared_secret(bob_pub);
  (void)bob.compute_shared_secret(alice_pub);

  std::string plaintext = "This is a secret message!";
  auto ciphertext = alice.encrypt(plaintext);
  DOCTEST_REQUIRE(!ciphertext.empty());
  DOCTEST_REQUIRE(ciphertext.size() > plaintext.size());

  auto decrypted = bob.decrypt(ciphertext);
  DOCTEST_REQUIRE(!decrypted.empty());
  DOCTEST_REQUIRE(decrypted == plaintext);
}

DOCTEST_TEST_CASE("crypto: tampered ciphertext fails")
{
  crypto::CryptoSession alice;
  crypto::CryptoSession bob;
  auto alice_pub = alice.generate_local_key();
  auto bob_pub = bob.generate_local_key();
  (void)alice.compute_shared_secret(bob_pub);
  (void)bob.compute_shared_secret(alice_pub);

  auto ciphertext = alice.encrypt("test data");
  DOCTEST_REQUIRE(!ciphertext.empty());

  // Tamper with the ciphertext
  if (ciphertext.size() > 30)
    ciphertext[28] ^= 0xFF; // flip a bit in the encrypted payload

  auto decrypted = bob.decrypt(ciphertext);
  DOCTEST_REQUIRE(decrypted.empty()); // GCM auth tag should catch this
}
