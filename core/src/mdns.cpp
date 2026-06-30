// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/mdns.hpp"
#include "wininspect/logger.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <vector>
#include <cstring>
#include <thread>

namespace wininspect {

// ── mDNS Constants ──────────────────────────────────────────────────────────

static constexpr int MDNS_PORT = 5353;
static constexpr const char *MDNS_ADDR = "224.0.0.251";

// DNS record types
static constexpr uint16_t DNS_TYPE_PTR = 12;
static constexpr uint16_t DNS_TYPE_SRV = 33;
static constexpr uint16_t DNS_TYPE_A = 1;
static constexpr uint16_t DNS_TYPE_TXT = 16;
static constexpr uint16_t DNS_TYPE_AAAA = 28;

// DNS class IN with mDNS unicast-response bit
static constexpr uint16_t DNS_CLASS_IN = 1;
static constexpr uint16_t DNS_CLASS_IN_FLUSH = 0x8001;

// ── Helpers ─────────────────────────────────────────────────────────────────

static uint16_t read_u16(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

static void write_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xFF);
}

static void write_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)(v & 0xFF);
}

std::vector<uint8_t> MdnsResponder::encode_name(const std::string &name) {
  std::vector<uint8_t> out;
  size_t start = 0;
  while (start < name.size()) {
    auto dot = name.find('.', start);
    if (dot == std::string::npos) dot = name.size();
    size_t len = dot - start;
    out.push_back((uint8_t)len);
    for (size_t i = 0; i < len; i++)
      out.push_back((uint8_t)name[start + i]);
    start = dot + 1;
  }
  out.push_back(0); // root label
  return out;
}

std::pair<std::string, int> MdnsResponder::decode_name(
    const std::vector<uint8_t> &packet, int offset) {
  std::string name;
  bool jumped = false;
  int jump_offset = 0;

  while (offset < (int)packet.size()) {
    uint8_t len = packet[offset];
    if (len == 0) {
      offset++;
      break;
    }
    // Check for compression pointer (top 2 bits set)
    if ((len & 0xC0) == 0xC0) {
      if (!jumped) {
        jump_offset = offset + 2;
        jumped = true;
      }
      offset = ((len & 0x3F) << 8) | packet[offset + 1];
      continue;
    }
    if (!name.empty()) name += '.';
    offset++;
    for (int i = 0; i < len && offset < (int)packet.size(); i++) {
      name += (char)packet[offset++];
    }
  }
  if (jumped) offset = jump_offset;
  return {name, offset};
}

// ── Build DNS Response ──────────────────────────────────────────────────────

std::vector<uint8_t> MdnsResponder::build_ptr_response(
    const std::vector<uint8_t> &query,
    const std::string &service_type,
    const std::string &instance_name,
    int port,
    const std::string &hostname,
    const std::string &txt_records) {

  std::vector<uint8_t> resp;
  resp.resize(12); // DNS header
  write_u16(resp.data(), 0x0); // ID = 0 (mDNS)
  write_u16(resp.data() + 2, 0x8400); // flags: response, authoritative

  // Copy question section from query (1 question)
  int qcount = read_u16(query.data() + 4);
  write_u16(resp.data() + 4, qcount); // question count
  write_u16(resp.data() + 6, 1);      // answer count
  write_u16(resp.data() + 8, 0);      // authority count
  write_u16(resp.data() + 10, 2);     // additional count (SRV + A)

  // Copy the question verbatim
  std::vector<uint8_t> qname = encode_name("_" + service_type + "._tcp.local");
  // Find where the question ends in the original query
  int qend = 12;
  // Skip past original question name
  auto decoded = decode_name(query, 12);
  qend = decoded.second + 4; // +4 for QTYPE and QCLASS

  // Write question
  resp.insert(resp.end(), qname.begin(), qname.end());
  uint8_t qtype_class[4];
  write_u16(qtype_class, DNS_TYPE_PTR);
  write_u16(qtype_class + 2, DNS_CLASS_IN);
  resp.insert(resp.end(), qtype_class, qtype_class + 4);

  // Answer: PTR record pointing to instance
  std::vector<uint8_t> ptr_name = encode_name("_" + service_type + "._tcp.local");
  std::vector<uint8_t> instance_full = encode_name(instance_name + "._" + service_type + "._tcp.local");
  uint8_t ptr_hdr[10];
  write_u16(ptr_hdr, DNS_TYPE_PTR);
  write_u16(ptr_hdr + 2, DNS_CLASS_IN_FLUSH);  // cache flush
  write_u32(ptr_hdr + 4, 120);  // TTL 120s
  write_u16(ptr_hdr + 8, (uint16_t)instance_full.size()); // data length
  resp.insert(resp.end(), ptr_name.begin(), ptr_name.end());
  resp.insert(resp.end(), ptr_hdr, ptr_hdr + 10);
  resp.insert(resp.end(), instance_full.begin(), instance_full.end());

  // Additional 1: SRV record
  std::vector<uint8_t> srv_name = instance_full;
  std::vector<uint8_t> target_name = encode_name(hostname + ".local.");
  uint8_t srv_data[6];
  write_u16(srv_data, 0);      // priority
  write_u16(srv_data + 2, 0);  // weight
  write_u16(srv_data + 4, (uint16_t)port);
  uint8_t srv_hdr[10];
  write_u16(srv_hdr, DNS_TYPE_SRV);
  write_u16(srv_hdr + 2, DNS_CLASS_IN_FLUSH);
  write_u32(srv_hdr + 4, 120);
  write_u16(srv_hdr + 8, (uint16_t)(6 + target_name.size()));
  resp.insert(resp.end(), srv_name.begin(), srv_name.end());
  resp.insert(resp.end(), srv_hdr, srv_hdr + 10);
  resp.insert(resp.end(), srv_data, srv_data + 6);
  resp.insert(resp.end(), target_name.begin(), target_name.end());

  // Additional 2: A record for the target hostname
  std::vector<uint8_t> a_name = encode_name(hostname + ".local.");
  uint8_t a_data[4] = {0, 0, 0, 0}; // 0.0.0.0 — client uses source addr
  uint8_t a_hdr[10];
  write_u16(a_hdr, DNS_TYPE_A);
  write_u16(a_hdr + 2, DNS_CLASS_IN_FLUSH);
  write_u32(a_hdr + 4, 120);   // TTL
  write_u16(a_hdr + 8, 4);     // data length (4 bytes for IPv4)
  resp.insert(resp.end(), a_name.begin(), a_name.end());
  resp.insert(resp.end(), a_hdr, a_hdr + 10);
  resp.insert(resp.end(), a_data, a_data + 4);

  // Fix header counts
  write_u16(resp.data() + 4, qcount);
  write_u16(resp.data() + 6, 1);
  write_u16(resp.data() + 10, 2);

  return resp;
}

// ── Start ───────────────────────────────────────────────────────────────────

void MdnsResponder::start(std::atomic<bool> *running,
                           const std::string &service_type,
                           int port,
                           const std::string &hostname,
                           const std::string &txt_records) {
#ifdef _WIN32
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) {
    LOG_WARN("mDNS: Failed to create socket");
    return;
  }
  sock_ = (uintptr_t)s;

  // Allow sharing the mDNS port
  int reuse = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(MDNS_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    int err = WSAGetLastError();
    LOG_WARN("mDNS: Cannot bind to port 5353 (err " + std::to_string(err) +
             ") — mDNS unavailable, falling back to UDP discovery");
    closesocket(s);
    sock_ = 0;
    return;
  }

  // Join mDNS multicast group
  struct ip_mreq mreq;
  mreq.imr_multiaddr.s_addr = inet_addr(MDNS_ADDR);
  mreq.imr_interface.s_addr = INADDR_ANY;
  if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                 (const char *)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
    LOG_WARN("mDNS: Cannot join multicast group — mDNS unavailable");
    closesocket(s);
    sock_ = 0;
    return;
  }

  // Set receive timeout so we can check running flag periodically
  DWORD timeout = 1000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

  LOG_INFO("mDNS responder listening on " + std::string(MDNS_ADDR) +
           ":" + std::to_string(MDNS_PORT) + " for _" + service_type + "._tcp");

  std::string service = "_" + service_type + "._tcp.local";
  std::string instance_name = hostname + " [" + std::to_string(port) + "]";

  uint8_t buf[1500];
  while (running->load()) {
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int r = recvfrom(s, (char *)buf, sizeof(buf), 0,
                     (struct sockaddr *)&from, &from_len);
    if (r <= 0) continue;

    // Check it's a DNS query with at least the header
    if (r < 12) continue;
    if ((buf[2] & 0x80) != 0) continue; // not a query

    // Check for PTR query for our service
    auto decoded = decode_name(std::vector<uint8_t>(buf, buf + r), 12);
    if (decoded.first != service) continue;

    uint16_t qtype = read_u16(buf + decoded.second);
    if (qtype != DNS_TYPE_PTR) continue;

    // Build and send response
    auto resp = build_ptr_response(
        std::vector<uint8_t>(buf, buf + r),
        service_type, instance_name, port, hostname, txt_records);

    sendto(s, (const char *)resp.data(), (int)resp.size(), 0,
           (struct sockaddr *)&from, from_len);

    LOG_DEBUG("mDNS: Responded to PTR query for " + service);
  }

  // Leave multicast group
  setsockopt(s, IPPROTO_IP, IP_DROP_MEMBERSHIP,
             (const char *)&mreq, sizeof(mreq));
  closesocket(s);
  sock_ = 0;
  LOG_INFO("mDNS responder stopped");
#else
  (void)running; (void)service_type; (void)port;
  (void)hostname; (void)txt_records;
  LOG_WARN("mDNS: Not implemented for non-Windows yet");
#endif
}

MdnsResponder::MdnsResponder() {}
MdnsResponder::~MdnsResponder() {
  if (sock_ != 0) {
#ifdef _WIN32
    closesocket((SOCKET)sock_);
#endif
  }
}

} // namespace wininspect
