#include <windows.h>
#include <cstring>
#include <iostream>

static bool focus_probe() {
  HWND hwnd = FindWindowW(L"WinInspectWineInputProbe", nullptr);
  if (!hwnd) return false;
  DWORD target_thread = GetWindowThreadProcessId(hwnd, nullptr);
  DWORD current_thread = GetCurrentThreadId();
  BOOL attached = FALSE;
  if (target_thread != current_thread) {
    attached = AttachThreadInput(current_thread, target_thread, TRUE);
  }
  SetForegroundWindow(hwnd);
  HWND edit = GetDlgItem(hwnd, 1001);
  if (edit) SetFocus(edit);
  if (attached) AttachThreadInput(current_thread, target_thread, FALSE);
  return true;
}

int main(int argc, char** argv) {
  if (!focus_probe()) {
    std::cerr << "probe_focus_failed error=" << GetLastError() << std::endl;
    return 2;
  }
  if (argc > 1 && std::strcmp(argv[1], "--focus-only") == 0) {
    std::cout << "probe_focus_ok" << std::endl;
    return 0;
  }

  INPUT input[2]{};
  input[0].type = INPUT_KEYBOARD;
  input[0].ki.wVk = 'A';
  input[1].type = INPUT_KEYBOARD;
  input[1].ki.wVk = 'A';
  input[1].ki.dwFlags = KEYEVENTF_KEYUP;
  const UINT sent = SendInput(2, input, sizeof(INPUT));
  std::cout << "sendinput_requested=2 sent=" << sent << " error=" << GetLastError() << std::endl;
  return sent == 2 ? 0 : 1;
}
