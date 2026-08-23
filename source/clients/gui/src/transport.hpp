#pragma once
// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Copyright (c) 2026 Mark E. DeYoung
//
// WinInspect GUI — Named-pipe transport for daemon communication.
// Extracted from gui_main.cpp for testability.

#ifdef _WIN32
#include <windows.h>
#include <string>
#include "viewmodel.hpp"

namespace wininspect_gui {

  /// Named-pipe transport to the WinInspect daemon.
  /// Connects to \\.\pipe\wininspectd with retry logic (3 attempts).
  /// Returns error JSON on failure instead of throwing.
  class PipeTransport : public ITransport
  {
  public:
    bool connected() const { return connected_; }

    std::string request(const std::string& json) override
    {
      for (int attempt = 0; attempt < 3; attempt++) {
        HANDLE h = CreateFileW(L"\\\\.\\pipe\\wininspectd",
                                GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
          connected_ = false;
          if (attempt < 2) Sleep(200);
          continue;
        }
        uint32_t len = (uint32_t)json.size();
        DWORD w = 0;
        if (!WriteFile(h, &len, 4, &w, nullptr) ||
            !WriteFile(h, json.data(), len, &w, nullptr)) {
          CloseHandle(h);
          continue;
        }
        uint32_t rlen = 0;
        DWORD r = 0;
        if (!ReadFile(h, &rlen, 4, &r, nullptr)) { CloseHandle(h); continue; }
        std::string resp;
        resp.resize(rlen);
        if (!ReadFile(h, resp.data(), rlen, &r, nullptr)) { CloseHandle(h); continue; }
        CloseHandle(h);
        connected_ = true;
        return resp;
      }
      connected_ = false;
      return "{\"ok\":false,\"error\":\"no daemon\"}";
    }

  private:
    bool connected_ = false;
  };

} // namespace wininspect_gui
#endif // _WIN32
