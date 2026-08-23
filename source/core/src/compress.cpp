// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung

#include "wininspect/compress.hpp"
#include "wininspect/logger.hpp"

// ── Platform Selection ─────────────────────────────────────────────────────
// The Windows Compression API (compressapi.h) is the primary target.
// MinGW (mingw-w64) provides compressapi.h since ~2015.
// Fallback to zlib if compressapi.h is absent.
// Non-Windows builds use empty stubs (no compression).

#if defined(_WIN32)

// Try Windows Compression API first (MSVC + modern MinGW)
#if __has_include(<compressapi.h>)
#define HAVE_COMPRESSAPI 1
#include <windows.h>
#include <compressapi.h>
#pragma comment(lib, "cabinet.lib")

static constexpr size_t COMPRESS_OVERHEAD = 512;

namespace wininspect {

  std::vector<uint8_t> compress(const std::vector<uint8_t>& data)
  {
    if (data.empty())
      return {};

    COMPRESSOR_HANDLE compressor = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &compressor)) {
      LOG_DEBUG("compress: CreateCompressor failed");
      return {};
    }

    SIZE_T compressed_size = data.size() + COMPRESS_OVERHEAD;
    std::vector<uint8_t> out(compressed_size);
    if (!Compress(compressor, data.data(), data.size(), out.data(), out.size(), &compressed_size)) {
      DWORD err = GetLastError();
      if (err == ERROR_INSUFFICIENT_BUFFER) {
        out.resize(compressed_size);
        if (!Compress(compressor, data.data(), data.size(), out.data(), out.size(),
                      &compressed_size)) {
          CloseCompressor(compressor);
          return {};
        }
      }
      else {
        CloseCompressor(compressor);
        return {};
      }
    }
    out.resize(compressed_size);
    CloseCompressor(compressor);
    return out;
  }

  std::vector<uint8_t> decompress(const std::vector<uint8_t>& data, size_t uncompressed_size)
  {
    if (data.empty() || uncompressed_size == 0)
      return {};

    DECOMPRESSOR_HANDLE decompressor = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &decompressor)) {
      LOG_DEBUG("decompress: CreateDecompressor failed");
      return {};
    }

    std::vector<uint8_t> out(uncompressed_size);
    SIZE_T raw_size = uncompressed_size;
    if (!Decompress(decompressor, data.data(), data.size(), out.data(), out.size(), &raw_size)) {
      LOG_DEBUG("decompress: Decompress failed");
      out.clear();
    }

    CloseDecompressor(decompressor);
    return out;
  }

} // namespace wininspect

#elif __has_include(<zlib.h>)
// MinGW / older Windows: use zlib's compress2/uncompress (raw deflate)
#include <zlib.h>

namespace wininspect {

  std::vector<uint8_t> compress(const std::vector<uint8_t>& data)
  {
    if (data.empty())
      return {};

    uLongf dest_len = compressBound((uLong)data.size());
    std::vector<uint8_t> out(dest_len);

    if (compress2(out.data(), &dest_len, data.data(), (uLong)data.size(), Z_BEST_SPEED) != Z_OK) {
      return {};
    }
    out.resize(dest_len);
    return out;
  }

  std::vector<uint8_t> decompress(const std::vector<uint8_t>& data, size_t uncompressed_size)
  {
    if (data.empty() || uncompressed_size == 0)
      return {};

    std::vector<uint8_t> out(uncompressed_size);
    uLongf dest_len = (uLong)uncompressed_size;
    if (uncompress(out.data(), &dest_len, data.data(), (uLong)data.size()) != Z_OK) {
      return {};
    }
    return out;
  }

} // namespace wininspect

#else
// Windows but neither compressapi.h nor zlib.h — empty stubs
#pragma message("compress.cpp: no compression library available, using stubs")
namespace wininspect {
  std::vector<uint8_t> compress(const std::vector<uint8_t>&)
  {
    return {};
  }
  std::vector<uint8_t> decompress(const std::vector<uint8_t>&, size_t)
  {
    return {};
  }
} // namespace wininspect
#endif

#else
// Non-Windows (Linux/Wine without MinGW) — empty stubs
namespace wininspect {
  std::vector<uint8_t> compress(const std::vector<uint8_t>&)
  {
    return {};
  }
  std::vector<uint8_t> decompress(const std::vector<uint8_t>&, size_t)
  {
    return {};
  }
} // namespace wininspect
#endif
