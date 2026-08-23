#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// Wire-level compression for large responses.
// Uses Windows Compression API (COMPRESS_ALGORITHM_MSZIP = deflate).
// Available on Windows 8+ (our target is Windows 10 0x0A00).
//
// Frame format:
//   If compressed bit (0x80000000) is set in length field:
//     [4B: compressed_len | 0x80000000][compressed_payload]
//   Else (legacy):
//     [4B: plain_len][plain_payload]
//
// Negotiation: client sends "accept_encoding":"zlib" during auth.
// Server echoes "content_encoding":"zlib" in auth_status when accepted.

#include <cstdint>
#include <string>
#include <vector>

namespace wininspect {

  /// Compress raw bytes using deflate (MSZIP).
  /// Returns empty vector on failure.
  std::vector<uint8_t> compress(const std::vector<uint8_t>& data);

  /// Decompress deflate-compressed bytes.
  /// Returns empty vector on failure (wrong-size buffers, corruption).
  std::vector<uint8_t> decompress(const std::vector<uint8_t>& data, size_t uncompressed_size);

  /// Framing: the MSB of the 4-byte length field signals compression.
  /// Max uncompressed payload is 10MB, which easily fits in 31 bits.
  inline constexpr uint32_t FRAME_COMPRESSED_FLAG = 0x80000000;

} // namespace wininspect
