#include <windows.h>
#include <iostream>

int main() {
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
