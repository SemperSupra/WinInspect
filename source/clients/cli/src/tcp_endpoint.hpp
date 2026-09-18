// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
#pragma once

#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>

namespace wininspect::cli {

  struct TcpEndpoint
  {
    std::string host;
    std::uint16_t port = 1985;
  };

  inline bool parse_port(const std::string& text, std::uint16_t& port)
  {
    unsigned int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        parsed == 0 || parsed > 65535)
      return false;
    port = static_cast<std::uint16_t>(parsed);
    return true;
  }

  // IPv6 endpoints with an explicit port must use RFC 3986 bracket notation.
  // An unbracketed string containing multiple colons is an IPv6 host using default_port.
  inline bool parse_tcp_endpoint(const std::string& text, std::uint16_t default_port,
                                 TcpEndpoint& endpoint)
  {
    if (text.empty())
      return false;

    endpoint.port = default_port;
    if (text.front() == '[') {
      const auto close = text.find(']');
      if (close == std::string::npos || close == 1)
        return false;
      endpoint.host = text.substr(1, close - 1);
      if (close + 1 == text.size())
        return true;
      if (text[close + 1] != ':' || close + 2 >= text.size())
        return false;
      return parse_port(text.substr(close + 2), endpoint.port);
    }

    const auto first_colon = text.find(':');
    if (first_colon == std::string::npos) {
      endpoint.host = text;
      return true;
    }
    if (text.find(':', first_colon + 1) != std::string::npos) {
      endpoint.host = text;
      return true;
    }
    if (first_colon == 0)
      return false;
    endpoint.host = text.substr(0, first_colon);
    return parse_port(text.substr(first_colon + 1), endpoint.port);
  }

} // namespace wininspect::cli
