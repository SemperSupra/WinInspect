#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include <string>
#include <vector>
#include <cstring>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace wininspect {

/// Parse a CIDR string like "192.168.1.0/24" or "::1/128" into a network
/// address and prefix length. Returns true on success.
bool parse_cidr(const std::string &cidr_str, struct sockaddr_storage &net,
                int &prefix_len);

/// Check if the given IP address matches a CIDR range.
/// addr_str: "192.168.1.50" or "::1"
/// cidr_str: "192.168.1.0/24" or "::1/128"
bool cidr_match(const std::string &addr_str, const std::string &cidr_str);

/// Check if an address matches any CIDR in a list.
bool cidr_match_any(const std::string &addr_str,
                    const std::vector<std::string> &cidr_list);

/// Convert a sockaddr to a string for logging.
std::string sockaddr_to_string(const struct sockaddr_storage &addr);

} // namespace wininspect
