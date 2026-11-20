// Public, standalone Win32/Mingw importability test; no private project code.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include <string>
#include <utility>
#include <iostream>
static std::string get_config_dir() {
  std::string appdata_dir;
  const DWORD required = GetEnvironmentVariableA("APPDATA", nullptr, 0);
  if (required > 1 && required <= 32768) {
    std::string value(required, '\0');
    const DWORD actual = GetEnvironmentVariableA("APPDATA", value.data(), required);
    if (actual > 0 && actual < required) {
      value.resize(actual);
      appdata_dir = std::move(value);
    }
  }
  if (!appdata_dir.empty()) return appdata_dir + "\\WinInspect";
  const char* profile = std::getenv("USERPROFILE");
  if (profile && profile[0]) return std::string(profile) + "\\.config\\wininspect";
  return ".\\.config\\wininspect";
}
int main() {
  const auto path=get_config_dir();
  if(path.empty()) return 2;
  std::cout << "APPDATA_WIN32_CRT_LINK=PASS\n";
  return 0;
}
