// SPDX-License-Identifier: PolyForm-NC-1.0.0
// Synthetic WinEvent feasibility probe: no private source, target IDs or user data.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace {
std::atomic<DWORD> target_pid{0};
std::atomic<unsigned> created{0};
std::atomic<unsigned> shown{0};
void CALLBACK on_event(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG object,
                       LONG child, DWORD, DWORD) {
  if (!hwnd || object != OBJID_WINDOW || child != CHILDID_SELF) return;
  DWORD owner = 0;
  GetWindowThreadProcessId(hwnd, &owner);
  if (!owner || owner != target_pid.load()) return;
  if (event == EVENT_OBJECT_CREATE) ++created;
  if (event == EVENT_OBJECT_SHOW) ++shown;
}
LRESULT CALLBACK fixture_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  return DefWindowProcW(h, m, w, l);
}
int producer() {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  constexpr wchar_t classname[] = L"WinInspectSyntheticWinEventFixture";
  WNDCLASSW wc{};
  wc.hInstance = instance;
  wc.lpfnWndProc = fixture_wndproc;
  wc.lpszClassName = classname;
  if (!RegisterClassW(&wc)) return 4;
  HWND hwnd = CreateWindowExW(0, classname, L"synthetic", WS_OVERLAPPEDWINDOW,
                              20, 20, 180, 120, nullptr, nullptr, instance, nullptr);
  if (!hwnd) {
    UnregisterClassW(classname, instance);
    return 4;
  }
  ShowWindow(hwnd, SW_SHOWNA);
  UpdateWindow(hwnd);
  SetWindowPos(hwnd, nullptr, 40, 40, 200, 130, SWP_NOZORDER | SWP_NOACTIVATE);
  Sleep(750);
  DestroyWindow(hwnd);
  UnregisterClassW(classname, instance);
  return 0;
}
int probe() {
  MSG msg{};
  PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
  const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
  HWINEVENTHOOK objects = SetWinEventHook(EVENT_OBJECT_CREATE,
      EVENT_OBJECT_LOCATIONCHANGE, nullptr, on_event, 0, 0, flags);
  HWINEVENTHOOK foreground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
      EVENT_SYSTEM_FOREGROUND, nullptr, on_event, 0, 0, flags);
  if (!objects || !foreground) {
    if (objects) UnhookWinEvent(objects);
    if (foreground) UnhookWinEvent(foreground);
    std::puts("RESULT=REGISTRATION_UNAVAILABLE");
    return 2;
  }
  wchar_t executable[MAX_PATH]{};
  DWORD n = GetModuleFileNameW(nullptr, executable, MAX_PATH);
  if (!n || n == MAX_PATH) {
    UnhookWinEvent(foreground);
    UnhookWinEvent(objects);
    std::puts("RESULT=PRODUCER_UNAVAILABLE");
    return 4;
  }
  std::wstring command = L"\"" + std::wstring(executable) + L"\" --producer";
  std::vector<wchar_t> cmd(command.begin(), command.end());
  cmd.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION child{};
  if (!CreateProcessW(executable, cmd.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) {
    UnhookWinEvent(foreground);
    UnhookWinEvent(objects);
    std::puts("RESULT=PRODUCER_UNAVAILABLE");
    return 4;
  }
  target_pid.store(child.dwProcessId);
  bool exited = false;
  const ULONGLONG deadline = GetTickCount64() + 7000;
  while (GetTickCount64() < deadline) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0) {
      exited = true;
      for (unsigned i = 0; i < 8; ++i) {
        Sleep(25);
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
      }
      break;
    }
    Sleep(25);
  }
  if (!exited) {
    TerminateProcess(child.hProcess, 4); // Only the synthetic child we created.
    WaitForSingleObject(child.hProcess, 1000);
  }
  DWORD status = 4;
  GetExitCodeProcess(child.hProcess, &status);
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);
  UnhookWinEvent(foreground);
  UnhookWinEvent(objects);
  target_pid.store(0);
  std::printf("REGISTERED=1\nPRODUCER_OK=%u\nCREATE_OBSERVED=%u\nSHOW_OBSERVED=%u\n",
              exited && status == 0 ? 1u : 0u, created.load(), shown.load());
  if (!exited || status != 0) {
    std::puts("RESULT=PRODUCER_FAILED");
    return 4;
  }
  if (!created.load() || !shown.load()) {
    std::puts("RESULT=DELIVERY_UNPROVEN");
    return 3;
  }
  std::puts("RESULT=SYNTHETIC_DELIVERY_OBSERVED");
  return 0;
}
}
int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--producer") == 0) return producer();
  if (argc == 2 && std::strcmp(argv[1], "--probe") == 0) return probe();
  std::puts("usage: native-winevent-probe --probe");
  return argc == 2 && std::strcmp(argv[1], "--help") == 0 ? 0 : 5;
}
