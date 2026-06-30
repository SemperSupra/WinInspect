// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/cidr.hpp"
#include <algorithm>
#include <sstream>

namespace wininspect {

bool parse_cidr(const std::string &cidr_str, struct sockaddr_storage &net,
                int &prefix_len) {
  std::memset(&net, 0, sizeof(net));

  // Split on '/'
  auto slash = cidr_str.find('/');
  if (slash == std::string::npos) return false;

  std::string addr_part = cidr_str.substr(0, slash);
  std::string prefix_part = cidr_str.substr(slash + 1);

  prefix_len = std::stoi(prefix_part);
  if (prefix_len < 0) return false;

  // Try IPv4 first
  struct sockaddr_in *sin = (struct sockaddr_in *)&net;
  if (inet_pton(AF_INET, addr_part.c_str(), &sin->sin_addr) == 1) {
    sin->sin_family = AF_INET;
    if (prefix_len > 32) return false;
    return true;
  }

  // Try IPv6
  struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&net;
  if (inet_pton(AF_INET6, addr_part.c_str(), &sin6->sin6_addr) == 1) {
    sin6->sin6_family = AF_INET6;
    if (prefix_len > 128) return false;
    return true;
  }

  return false;
}

bool cidr_match(const std::string &addr_str, const std::string &cidr_str) {
  struct sockaddr_storage net;
  int prefix_len = 0;
  if (!parse_cidr(cidr_str, net, prefix_len)) return false;

  // Parse the address to check
  struct sockaddr_storage addr;
  std::memset(&addr, 0, sizeof(addr));

  struct sockaddr_in *addr_in = (struct sockaddr_in *)&addr;
  struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&addr;

  if (net.ss_family == AF_INET) {
    if (inet_pton(AF_INET, addr_str.c_str(), &addr_in->sin_addr) != 1)
      return false;
    addr_in->sin_family = AF_INET;

    uint32_t net_addr = ntohl(((struct sockaddr_in *)&net)->sin_addr.s_addr);
    uint32_t test_addr = ntohl(addr_in->sin_addr.s_addr);
    uint32_t mask = (prefix_len == 0) ? 0 : (uint32_t)(~0ULL << (32 - prefix_len));

    return (net_addr & mask) == (test_addr & mask);
  }

  if (net.ss_family == AF_INET6) {
    if (inet_pton(AF_INET6, addr_str.c_str(), &addr_in6->sin6_addr) != 1)
      return false;
    addr_in6->sin6_family = AF_INET6;

    // Compare 128-bit addresses, prefix_len bits at a time
    auto *net_bytes = (const uint8_t *)&(((struct sockaddr_in6 *)&net)->sin6_addr);
    auto *test_bytes = (const uint8_t *)&addr_in6->sin6_addr;

    int full_bytes = prefix_len / 8;
    int remaining_bits = prefix_len % 8;

    for (int i = 0; i < full_bytes; i++) {
      if (net_bytes[i] != test_bytes[i]) return false;
    }

    if (remaining_bits > 0) {
      uint8_t mask = (uint8_t)(0xFF << (8 - remaining_bits));
      if ((net_bytes[full_bytes] & mask) != (test_bytes[full_bytes] & mask))
        return false;
    }

    return true;
  }

  return false;
}

bool cidr_match_any(const std::string &addr_str,
                    const std::vector<std::string> &cidr_list) {
  for (auto &cidr : cidr_list) {
    if (cidr_match(addr_str, cidr)) return true;
  }
  return false;
}

std::string sockaddr_to_string(const struct sockaddr_storage &addr) {
  char buf[64] = {};
  if (addr.ss_family == AF_INET) {
    auto *sin = (const struct sockaddr_in *)&addr;
    inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
  } else if (addr.ss_family == AF_INET6) {
    auto *sin6 = (const struct sockaddr_in6 *)&addr;
    inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf));
  }
  return std::string(buf);
}

} // namespace wininspect
