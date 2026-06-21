#pragma once
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wininspect::base64 {

[[nodiscard]] inline std::vector<uint8_t> decode(std::string_view in) {
  static const std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<uint8_t> out;
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++)
    T[static_cast<unsigned char>(alphabet[i])] = i;

  int val = 0, valb = -8;
  for (char c : in) {
    if (T[static_cast<unsigned char>(c)] == -1)
      break;
    val = (val << 6) + T[static_cast<unsigned char>(c)];
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

[[nodiscard]] inline std::string encode(const std::vector<uint8_t> &in) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, valb = -6;
  for (uint8_t c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(alphabet[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6)
    out.push_back(alphabet[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4)
    out.push_back('=');
  return out;
}

} // namespace wininspect::base64
