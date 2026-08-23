// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "doctest/doctest.h"
#include "wininspect/mdns.hpp"
#include <cstring>

using namespace wininspect;

// ── DNS name encoding/decoding ──────────────────────────────────────────────

DOCTEST_TEST_CASE("mdns: encode_name basic")
{
  auto enc = MdnsResponder::encode_name("test.local");
  REQUIRE(enc.size() >= 2);
  CHECK(enc[0] == 4); // label length "test"
  CHECK(enc[1] == 't');
  CHECK(enc[2] == 'e');
  CHECK(enc[3] == 's');
  CHECK(enc[4] == 't');
  CHECK(enc[5] == 5); // label length "local"
  CHECK(enc[6] == 'l');
  CHECK(enc.back() == 0); // root terminator at end
}

DOCTEST_TEST_CASE("mdns: encode_name empty terminates")
{
  auto enc = MdnsResponder::encode_name("");
  CHECK(enc.size() == 1);
  CHECK(enc[0] == 0);
}

DOCTEST_TEST_CASE("mdns: encode_name trailing dot")
{
  auto enc = MdnsResponder::encode_name("test.local.");
  CHECK(enc.back() == 0);
  // Should encode "test" then "local" then root
  CHECK(enc[0] == 4);
}

DOCTEST_TEST_CASE("mdns: encode_name single label")
{
  auto enc = MdnsResponder::encode_name("wininspect");
  CHECK(enc.size() == 12); // 10 chars + 1 length byte + 1 root
  CHECK(enc[0] == 10);     // label length
  CHECK(enc.back() == 0);  // root terminator
}

DOCTEST_TEST_CASE("mdns: decode_name round-trip")
{
  auto enc = MdnsResponder::encode_name("_wininspect._tcp.local");
  auto [decoded, offset] = MdnsResponder::decode_name(enc, 0);
  CHECK(decoded == "_wininspect._tcp.local");
  CHECK(offset == (int)enc.size());
}

DOCTEST_TEST_CASE("mdns: decode_name handles compression pointer")
{
  // Create a packet with name compression:
  // offset 0: name "\x04test\x00" (test.)
  // offset 7: pointer to offset 0: \xC0\x00
  std::vector<uint8_t> packet = {
      4,    't', 'e', 's', 't', 0, // "test." at offset 0
      0xC0, 0x00                   // pointer to offset 0
  };
  auto [name, off] = MdnsResponder::decode_name(packet, 6);
  CHECK(name == "test");
  CHECK(off == 8); // consumed 2 bytes for pointer
}

// ── DNS response building ─────────────────────────────────────────────────

DOCTEST_TEST_CASE("mdns: build_ptr_response has correct header")
{
  // Build a minimal mDNS PTR query packet
  std::vector<uint8_t> query;
  uint8_t header[] = {
      0, 0,            // ID
      0, 0,            // flags: query
      0, 1,            // 1 question
      0, 0, 0, 0, 0, 0 // answer, authority, additional = 0
  };
  query.insert(query.end(), header, header + 12);
  auto qname = MdnsResponder::encode_name("_wininspect._tcp.local");
  query.insert(query.end(), qname.begin(), qname.end());
  uint8_t qtype_class[] = {0, 12, 0, 1}; // TYPE PTR, CLASS IN
  query.insert(query.end(), qtype_class, qtype_class + 4);

  auto resp = MdnsResponder::build_ptr_response(query, "_wininspect", "MyPC", 1985, "mypc", "");

  REQUIRE(resp.size() > 12);

  // Check header flags: response + authoritative (0x8400)
  CHECK(resp[2] == 0x84);
  CHECK(resp[3] == 0x00);

  // Question count should match query
  CHECK(resp[4] == 0);
  CHECK(resp[5] == 1);

  // Exactly 1 answer record
  CHECK(resp[6] == 0);
  CHECK(resp[7] == 1);

  // 2 additional records (SRV + A)
  CHECK(resp[10] == 0);
  CHECK(resp[11] == 2);
}

DOCTEST_TEST_CASE("mdns: build_ptr_response contains PTR record")
{
  std::vector<uint8_t> query;
  uint8_t header[12] = {};
  header[5] = 1; // 1 question
  query.insert(query.end(), header, header + 12);
  auto qname = MdnsResponder::encode_name("_test._tcp.local");
  query.insert(query.end(), qname.begin(), qname.end());
  uint8_t qtype_class[] = {0, 12, 0, 1};
  query.insert(query.end(), qtype_class, qtype_class + 4);

  auto resp = MdnsResponder::build_ptr_response(query, "_test", "Server1", 9999, "box", "");

  // Find the PTR answer section (after question)
  int pos = 12 + (int)qname.size() + 4; // header + question

  // The response writes the PTR question name first, then the answer
  // Answer type should be PTR (12)
  CHECK(pos + 4 < (int)resp.size());

  // Skip past the name encoding in the answer section to find the type
  // The type is at varying offsets depending on name compression
  // Let's verify by searching for the PTR type value
  bool found_ptr = false;
  for (size_t i = 0; i < resp.size() - 1; i++) {
    if (resp[i] == 0 && resp[i + 1] == 12) { // TYPE_PTR = 12
      found_ptr = true;
      break;
    }
  }
  CHECK(found_ptr);
}

DOCTEST_TEST_CASE("mdns: build_ptr_response TTL is reasonable")
{
  std::vector<uint8_t> query;
  uint8_t header[12] = {};
  header[5] = 1;
  query.insert(query.end(), header, header + 12);
  auto qname = MdnsResponder::encode_name("_srv._tcp.local");
  query.insert(query.end(), qname.begin(), qname.end());
  uint8_t qtype_class[] = {0, 12, 0, 1};
  query.insert(query.end(), qtype_class, qtype_class + 4);

  auto resp = MdnsResponder::build_ptr_response(query, "_srv", "Node", 8080, "node1", "");

  // TTL should be 120 seconds at some offset in the response
  bool found_ttl = false;
  for (size_t i = 0; i < resp.size() - 3; i++) {
    // TTL = 4 bytes, value 120 = 0x00000078
    if (resp[i] == 0 && resp[i + 1] == 0 && resp[i + 2] == 0 && resp[i + 3] == 120) {
      found_ttl = true;
      break;
    }
  }
  CHECK(found_ttl);
}

// ── Edge cases ──────────────────────────────────────────────────────────────

DOCTEST_TEST_CASE("mdns: encode_name handles underscores and hyphens")
{
  auto enc = MdnsResponder::encode_name("_my-service_v2._tcp.local");
  // Should handle underscores and hyphens without issues
  CHECK(enc[0] == 14); // "_my-service_v2" is 14 chars
  CHECK(enc[15] == 4); // "_tcp" is 4 chars
}

DOCTEST_TEST_CASE("mdns: decode_name round-trip with underscores")
{
  std::string original = "_wininspect._tcp.local";
  auto enc = MdnsResponder::encode_name(original);
  auto [decoded, _] = MdnsResponder::decode_name(enc, 0);
  CHECK(decoded == original);
}
