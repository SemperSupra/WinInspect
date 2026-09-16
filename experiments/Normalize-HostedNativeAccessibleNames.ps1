$ErrorActionPreference = 'Stop'
$path = 'source/clients/gui/src/gui_main.cpp'
$cmakePath = 'source/CMakeLists.txt'
$text = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")
$cmake = [IO.File]::ReadAllText($cmakePath).Replace("`r`n", "`n")

function Replace-Exactly {
    param([string]$Old,[string]$New,[string]$Label)
    $first = $script:text.IndexOf($Old,[StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Expected $Label block not found." }
    if ($script:text.IndexOf($Old,$first + $Old.Length,[StringComparison]::Ordinal) -ge 0) {
        throw "Expected exactly one $Label block."
    }
    $script:text = $script:text.Substring(0,$first) + $New + $script:text.Substring($first + $Old.Length)
}

# Adopt Microsoft's documented native Win32 dynamic-annotation mechanism instead of
# inventing a custom UIA provider. Axe.Windows identified exactly two focusable controls
# with null Name: Search (ID 602) and the window-property list (ID 102).
Replace-Exactly @'
#include <windows.h>
'@ @'
#include <windows.h>
// oleacc.h uses DEFINE_GUID for the dynamic-annotation identifiers; instantiate
// them in this GUI translation unit exactly as Microsoft's accessibility samples do.
#include <initguid.h>
#include <oleacc.h>
#include <objbase.h>
'@ 'accessibility includes'

Replace-Exactly @'
#pragma comment(lib, "comctl32.lib")
'@ @'
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "oleacc.lib")
'@ 'OleAcc linkage'

Replace-Exactly @'
    createControls();
    refresh();
'@ @'
    createControls();
    initializeAccessibleNames();
    refresh();
'@ 'accessible-name initialization'

Replace-Exactly @'
  HWND hwnd_ = nullptr;

  // Sidebar + panels
'@ @'
  HWND hwnd_ = nullptr;
  IAccPropServices* accProps_ = nullptr;
  bool comInitializedHere_ = false;

  // Sidebar + panels
'@ 'accessibility state'

Replace-Exactly @'
  // ── Controls ──────────────────────────────────────────────────────────

  void createControls() {
'@ @'
  // ── Accessibility ─────────────────────────────────────────────────────

  void initializeAccessibleNames() {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(init))
      comInitializedHere_ = true;
    else if (init != RPC_E_CHANGED_MODE)
      return;

    if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&accProps_))))
      return;

    // These names describe purpose, never current value/content. This follows
    // Microsoft's Win32 guidance and is surfaced through UI Automation.
    accProps_->SetHwndPropStr(hSearchEdit_, OBJID_CLIENT, CHILDID_SELF,
                              PROPID_ACC_NAME, L"Search windows");
    accProps_->SetHwndPropStr(hList_, OBJID_CLIENT, CHILDID_SELF,
                              PROPID_ACC_NAME, L"Window properties");
    SendMessageW(hSearchEdit_, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search windows");
  }

  void clearAccessibleNames() {
    if (accProps_) {
      MSAAPROPID props[] = { PROPID_ACC_NAME };
      accProps_->ClearHwndProps(hSearchEdit_, OBJID_CLIENT, CHILDID_SELF, props, 1);
      accProps_->ClearHwndProps(hList_, OBJID_CLIENT, CHILDID_SELF, props, 1);
      accProps_->Release();
      accProps_ = nullptr;
    }
    if (comInitializedHere_) {
      CoUninitialize();
      comInitializedHere_ = false;
    }
  }

  // ── Controls ──────────────────────────────────────────────────────────

  void createControls() {
'@ 'accessibility helpers'

Replace-Exactly @'
      case WM_DESTROY:
        KillTimer(hwnd, IDT_REFRESH); KillTimer(hwnd, IDT_RETRY);
        PostQuitMessage(0); return 0;
'@ @'
      case WM_DESTROY:
        KillTimer(hwnd, IDT_REFRESH); KillTimer(hwnd, IDT_RETRY);
        self->clearAccessibleNames();
        PostQuitMessage(0); return 0;
'@ 'accessibility cleanup'

$oldLink = '  target_link_libraries(wininspect-gui PRIVATE wininspect_core comctl32)'
$newLink = '  target_link_libraries(wininspect-gui PRIVATE wininspect_core comctl32 oleacc)'
$first = $cmake.IndexOf($oldLink,[StringComparison]::Ordinal)
if ($first -lt 0) { throw 'Expected WinInspect GUI link block not found.' }
if ($cmake.IndexOf($oldLink,$first + $oldLink.Length,[StringComparison]::Ordinal) -ge 0) {
    throw 'Expected exactly one WinInspect GUI link block.'
}
$cmake = $cmake.Substring(0,$first) + $newLink + $cmake.Substring($first + $oldLink.Length)

[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($cmakePath,$cmake,[Text.UTF8Encoding]::new($false))
Write-Host 'Win32 accessible names normalized for Search windows and Window properties using IAccPropServices.'
