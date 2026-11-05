#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::atomic<unsigned long long> g_sequence{0};
HWND g_edit = nullptr;
HWND g_button = nullptr;

std::string utf8(const std::wstring& input) {
  if (input.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), out.data(), size, nullptr, nullptr);
  return out;
}

std::string escape_json(const std::string& input) {
  std::ostringstream out;
  for (unsigned char c : input) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
        else out << static_cast<char>(c);
    }
  }
  return out.str();
}

std::string ptr_string(HANDLE handle) {
  std::ostringstream out;
  out << "0x" << std::hex << reinterpret_cast<unsigned long long>(handle);
  return out.str();
}

void emit(const std::string& event, const std::string& fields = {}) {
  const auto seq = ++g_sequence;
  const auto tick = GetTickCount64();
  std::cout << "{\"seq\":" << seq << ",\"tick_ms\":" << tick << ",\"event\":\"" << escape_json(event) << "\"";
  if (!fields.empty()) std::cout << "," << fields;
  std::cout << "}" << std::endl;
}

std::wstring raw_device_name(HANDLE device) {
  if (device == nullptr) return L"";
  UINT chars = 0;
  if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &chars) == static_cast<UINT>(-1) || chars == 0) return L"";
  std::vector<wchar_t> buffer(static_cast<size_t>(chars) + 1, L'\0');
  UINT actual = chars;
  if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, buffer.data(), &actual) == static_cast<UINT>(-1)) return L"";
  return std::wstring(buffer.data());
}

void emit_raw_input(LPARAM lparam) {
  UINT size = 0;
  if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) || size == 0) {
    emit("raw_input_error", "\"stage\":\"size\"");
    return;
  }
  std::vector<BYTE> buffer(size);
  UINT actual = size;
  if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, buffer.data(), &actual, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
    emit("raw_input_error", "\"stage\":\"read\"");
    return;
  }
  const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
  std::ostringstream fields;
  fields << "\"hDevice\":\"" << ptr_string(raw->header.hDevice) << "\",\"device_name\":\"" << escape_json(utf8(raw_device_name(raw->header.hDevice))) << "\"";
  if (raw->header.dwType == RIM_TYPEKEYBOARD) {
    const auto& k = raw->data.keyboard;
    fields << ",\"type\":\"keyboard\",\"make_code\":" << k.MakeCode << ",\"flags\":" << k.Flags << ",\"vkey\":" << k.VKey << ",\"message\":" << k.Message;
    emit("raw_input", fields.str());
  } else if (raw->header.dwType == RIM_TYPEMOUSE) {
    const auto& m = raw->data.mouse;
    fields << ",\"type\":\"mouse\",\"flags\":" << m.usFlags << ",\"button_flags\":" << m.usButtonFlags << ",\"button_data\":" << m.usButtonData << ",\"dx\":" << m.lLastX << ",\"dy\":" << m.lLastY;
    emit("raw_input", fields.str());
  }
}

std::string text_of(HWND hwnd) {
  const int length = GetWindowTextLengthW(hwnd);
  if (length <= 0) return {};
  std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(hwnd, buffer.data(), length + 1);
  return escape_json(utf8(std::wstring(buffer.data())));
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 20, 20, 420, 30, hwnd, reinterpret_cast<HMENU>(1001), GetModuleHandleW(nullptr), nullptr);
      g_button = CreateWindowExW(0, L"BUTTON", L"Probe Button", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 20, 70, 140, 32, hwnd, reinterpret_cast<HMENU>(1002), GetModuleHandleW(nullptr), nullptr);
      emit("window_created");
      return 0;
    case WM_SETFOCUS: emit("focus_gained"); return 0;
    case WM_KILLFOCUS: emit("focus_lost"); return 0;
    case WM_INPUT:
      emit_raw_input(lparam);
      return DefWindowProcW(hwnd, message, wparam, lparam);
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
      std::ostringstream fields;
      fields << "\"message\":" << message << ",\"vkey\":" << wparam << ",\"lparam\":" << static_cast<unsigned long long>(lparam);
      emit("key_message", fields.str());
      break;
    }
    case WM_COMMAND: {
      const WORD id = LOWORD(wparam);
      const WORD code = HIWORD(wparam);
      if (id == 1001 && code == EN_CHANGE) emit("edit_changed", "\"text\":\"" + text_of(g_edit) + "\"");
      else if (id == 1002 && code == BN_CLICKED) emit("button_clicked");
      break;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: emit("window_destroyed"); PostQuitMessage(0); return 0;
    default: break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
}  // namespace

int main() {
  SetConsoleOutputCP(CP_UTF8);
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  const wchar_t* class_name = L"WinInspectWineInputProbe";
  WNDCLASSW wc{};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.lpszClassName = class_name;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (!RegisterClassW(&wc)) return 2;
  HWND hwnd = CreateWindowExW(0, class_name, L"WinInspect Input Probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 180, nullptr, nullptr, instance, nullptr);
  if (!hwnd) return 3;

  RAWINPUTDEVICE devices[2]{};
  devices[0].usUsagePage = 0x01; devices[0].usUsage = 0x02; devices[0].dwFlags = RIDEV_INPUTSINK; devices[0].hwndTarget = hwnd;
  devices[1].usUsagePage = 0x01; devices[1].usUsage = 0x06; devices[1].dwFlags = RIDEV_INPUTSINK; devices[1].hwndTarget = hwnd;
  if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) emit("raw_registration_failed", "\"error\":" + std::to_string(GetLastError()));
  else emit("raw_registration_ok");

  SetForegroundWindow(hwnd);
  if (g_edit != nullptr) SetFocus(g_edit);
  emit("probe_ready", "\"hwnd\":\"" + ptr_string(hwnd) + "\",\"pid\":" + std::to_string(GetCurrentProcessId()));

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}
