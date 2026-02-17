#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <commctrl.h>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

#include "viewmodel.hpp"
#include "wininspect/tinyjson.hpp"

#pragma comment(lib, "comctl32.lib")

using namespace wininspect_gui;

// Simple pipe transport for the GUI
class PipeTransport : public ITransport {
public:
<<<<<<< HEAD
    std::string request(const std::string& json) override {
        HANDLE h = CreateFileW(L"\\\\.\\pipe\\wininspectd", GENERIC_READ|GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return "{\"ok\":false,\"error\":\"no daemon\"}";
        
        uint32_t len = (uint32_t)json.size();
        DWORD w = 0;
        WriteFile(h, &len, 4, &w, nullptr);
        WriteFile(h, json.data(), len, &w, nullptr);

        uint32_t rlen = 0;
        DWORD r = 0;
        if (!ReadFile(h, &rlen, 4, &r, nullptr)) { CloseHandle(h); return "{\"ok\":false}"; }
        std::string resp; resp.resize(rlen);
        ReadFile(h, resp.data(), rlen, &r, nullptr);
        CloseHandle(h);
        return resp;
=======
  std::string request(const std::string &json) override {
    HANDLE h = CreateFileW(L"\\\\.\\pipe\\wininspectd", GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
      return "{\"ok\":false,\"error\":\"no daemon\"}";

    uint32_t len = (uint32_t)json.size();
    DWORD w = 0;
    WriteFile(h, &len, 4, &w, nullptr);
    WriteFile(h, json.data(), len, &w, nullptr);

    uint32_t rlen = 0;
    DWORD r = 0;
    if (!ReadFile(h, &rlen, 4, &r, nullptr)) {
      CloseHandle(h);
      return "{" ok ":false}";
>>>>>>> origin/master
    }
    std::string resp;
    resp.resize(rlen);
    ReadFile(h, resp.data(), rlen, &r, nullptr);
    CloseHandle(h);
    return resp;
  }
};

class WinInspectWindow {
public:
  bool init(HINSTANCE hInst) {
    hInst_ = hInst;
    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst_;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"WinInspectGUI";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    if (!RegisterClassExW(&wc))
      return false;

    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"WinInspect",
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            800, 600, nullptr, nullptr, hInst_, this);
    if (!hwnd_)
      return false;

    transport_ = std::make_unique<PipeTransport>();
    vm_ = std::make_unique<ViewModel>(transport_.get());

    createControls();
    refresh();

    return true;
  }

  void show(int nCmdShow) {
    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
  }

private:
  static LRESULT CALLBACK wndProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                  LPARAM lParam) {
    WinInspectWindow *self = nullptr;
    if (uMsg == WM_NCCREATE) {
      CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
      self = (WinInspectWindow *)cs->lpCreateParams;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
      self = (WinInspectWindow *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    if (self) {
      switch (uMsg) {
      case WM_SIZE:
        self->onSize();
        return 0;
      case WM_NOTIFY:
        self->onNotify(lParam);
        return 0;
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
      }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  }

  void createControls() {
    hTree_ =
        CreateWindowExW(0, WC_TREEVIEWW, L"",
                        WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASBUTTONS |
                            TVS_LINESATROOT | TVS_HASLINES,
                        0, 0, 200, 600, hwnd_, (HMENU)101, hInst_, nullptr);
    hList_ = CreateWindowExW(
        0, WC_LISTVIEWW, L"", WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT,
        200, 0, 600, 600, hwnd_, (HMENU)102, hInst_, nullptr);

    ListView_SetExtendedListViewStyle(hList_,
                                      LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 150;
    col.pszText = (LPWSTR)L"Property";
    ListView_InsertColumn(hList_, 0, &col);
    col.cx = 400;
    col.pszText = (LPWSTR)L"Value";
    ListView_InsertColumn(hList_, 1, &col);
  }

  void onSize() {
    RECT r;
    GetClientRect(hwnd_, &r);
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    int split = 250;
    MoveWindow(hTree_, 0, 0, split, h, TRUE);
    MoveWindow(hList_, split, 0, w - split, h, TRUE);
  }

  void refresh() {
    vm_->refresh();
    TreeView_DeleteAllItems(hTree_);
    hwnd_storage_.clear(); // Clear old strings to prevent leaks
    for (const auto &node : vm_->tree()) {
      addNode(TVI_ROOT, node);
    }
  }

  void addNode(HTREEITEM parent, const Node &n) {
    TVINSERTSTRUCTW tvi = {0};
    tvi.hParent = parent;
    tvi.hInsertAfter = TVI_LAST;
    tvi.item.mask = TVIF_TEXT | TVIF_PARAM;
    std::wstring wlabel(n.label.begin(), n.label.end()); // Simple conversion
    tvi.item.pszText = (LPWSTR)wlabel.c_str();

    // Use a managed vector for HWND strings to avoid memory leaks
    hwnd_storage_.push_back(n.hwnd);
    tvi.item.lParam = (LPARAM)(hwnd_storage_.size() - 1);

    HTREEITEM hItem = TreeView_InsertItem(hTree_, &tvi);
    for (const auto &child : n.children)
      addNode(hItem, child);
  }

  void onNotify(LPARAM lParam) {
    LPNMHDR nm = (LPNMHDR)lParam;
    if (nm->code == TVN_SELCHANGEDW) {
      LPNMTREEVIEWW nmtv = (LPNMTREEVIEWW)lParam;
      size_t idx = (size_t)nmtv->itemNew.lParam;
      if (idx < hwnd_storage_.size()) {
        vm_->select_hwnd(hwnd_storage_[idx]);
        updateProps();
      }
    }
  }

  void updateProps() {
    ListView_DeleteAllItems(hList_);
    int i = 0;
    for (const auto &p : vm_->props()) {
      LVITEMW item = {0};
      item.mask = LVIF_TEXT;
      item.iItem = i;
      item.iSubItem = 0;
      std::wstring wk(p.key.begin(), p.key.end());
      item.pszText = (LPWSTR)wk.c_str();
      ListView_InsertItem(hList_, &item);

      std::wstring wv(p.value.begin(), p.value.end());
      ListView_SetItemText(hList_, i, 1, (LPWSTR)wv.c_str());
      i++;
    }
  }

  HWND hwnd_ = nullptr;
  HWND hTree_ = nullptr;
  HWND hList_ = nullptr;
  HINSTANCE hInst_ = nullptr;
  std::unique_ptr<ViewModel> vm_;
  std::unique_ptr<ITransport> transport_;
  std::vector<std::string> hwnd_storage_;
};

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
  INITCOMMONCONTROLSEX icc{sizeof(icc),
                           ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
  InitCommonControlsEx(&icc);

  WinInspectWindow win;
  if (!win.init(hInst))
    return 1;
  win.show(nCmdShow);

  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}
#else
int main() { return 0; }
#endif
