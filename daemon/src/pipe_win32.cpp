#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "pipe.hpp"

namespace wininspectd {

static bool read_all(HANDLE h, void* buf, DWORD n) {
  BYTE* p = (BYTE*)buf;
  DWORD got = 0;
  while (got < n) {
    DWORD r = 0;
    if (!ReadFile(h, p + got, n - got, &r, nullptr)) return false;
    if (r == 0) return false;
    got += r;
  }
  return true;
}

static bool write_all(HANDLE h, const void* buf, DWORD n) {
  const BYTE* p = (const BYTE*)buf;
  DWORD sent = 0;
  while (sent < n) {
    DWORD w = 0;
    if (!WriteFile(h, p + sent, n - sent, &w, nullptr)) return false;
    if (w == 0) return false;
    sent += w;
  }
  return true;
}

bool pipe_read_message(void* hPipeV, PipeMessage& out) {
  HANDLE h = (HANDLE)hPipeV;
  std::uint32_t len = 0;
  if (!read_all(h, &len, sizeof(len))) return false;
  std::string s;
  s.resize(len);
  if (!read_all(h, s.data(), len)) return false;
  out.json = std::move(s);
  return true;
}

bool pipe_write_message(void* hPipeV, const std::string& json) {
  HANDLE h = (HANDLE)hPipeV;
  std::uint32_t len = (std::uint32_t)json.size();
  if (!write_all(h, &len, sizeof(len))) return false;
  return write_all(h, json.data(), len);
}

} // namespace wininspectd
#endif
