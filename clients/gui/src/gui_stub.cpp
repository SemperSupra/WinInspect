#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <commctrl.h>
#include <windows.h>
#pragma comment(lib, "comctl32.lib")

// This is an intentionally minimal GUI stub. The testable logic lives in
// ViewModel. Expand to TreeView + ListView shell.

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
  INITCOMMONCONTROLSEX icc{sizeof(icc),
                           ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
  InitCommonControlsEx(&icc);

  MessageBoxW(nullptr,
              L"WinInspect GUI stub. Implement TreeView/ListView shell next.",
              L"WinInspect", MB_OK);
  return 0;
}
#else
int main() { return 0; }
#endif
