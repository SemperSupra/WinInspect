#include <UIAutomation.h>
#include <atomic>
#include <iostream>
#include <string>
#include <windows.h>

// Standard COM Globals
std::atomic<long> g_refCount(0);
HINSTANCE g_hInst = NULL;

// Define a CLSID for our proxy (this would be the CLSID we register to override
// or augment) For example, if we were replacing CUIAutomation, we might use
// that CLSID, but usually we want a unique one and then register it as a
// provider. Here we just use a dummy GUID.
// {12345678-1234-1234-1234-123456789ABC}
static const GUID CLSID_WineUIAExtension = {
    0x12345678,
    0x1234,
    0x1234,
    {0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}};

// Helper to convert GUID to string
std::wstring GuidToString(const GUID &guid) {
  wchar_t buf[39];
  StringFromGUID2(guid, buf, 39);
  return std::wstring(buf);
}

// Registry helpers
bool SetRegistryKey(HKEY hKeyRoot, const std::wstring &subKey,
                    const std::wstring &valueName, const std::wstring &data) {
  HKEY hKey;
  LONG lResult =
      RegCreateKeyExW(hKeyRoot, subKey.c_str(), 0, NULL,
                      REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
  if (lResult != ERROR_SUCCESS)
    return false;

  lResult = RegSetValueExW(hKey, valueName.empty() ? NULL : valueName.c_str(),
                           0, REG_SZ, (const BYTE *)data.c_str(),
                           (DWORD)(data.size() + 1) * sizeof(wchar_t));

  RegCloseKey(hKey);
  return lResult == ERROR_SUCCESS;
}

bool DeleteRegistryKey(HKEY hKeyRoot, const std::wstring &subKey) {
  return RegDeleteTreeW(hKeyRoot, subKey.c_str()) == ERROR_SUCCESS;
}

class CMyUIAProxy : public IUnknown {
  std::atomic<long> m_refCount;

public:
  CMyUIAProxy() : m_refCount(1) { g_refCount++; }
  virtual ~CMyUIAProxy() { g_refCount--; }

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override {
    if (!ppvObject)
      return E_POINTER;
    if (riid == IID_IUnknown) {
      *ppvObject = static_cast<IUnknown *>(this);
      AddRef();
      return S_OK;
    }
    // TODO: Implement IUIAutomation or IRawElementProviderSimple here
    *ppvObject = NULL;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }

  ULONG STDMETHODCALLTYPE Release() override {
    long val = --m_refCount;
    if (val == 0)
      delete this;
    return val;
  }
};

class CClassFactory : public IClassFactory {
  std::atomic<long> m_refCount;

public:
  CClassFactory() : m_refCount(1) { g_refCount++; }
  virtual ~CClassFactory() { g_refCount--; }

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override {
    if (!ppvObject)
      return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
      *ppvObject = static_cast<IClassFactory *>(this);
      AddRef();
      return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }

  ULONG STDMETHODCALLTYPE Release() override {
    long val = --m_refCount;
    if (val == 0)
      delete this;
    return val;
  }

  // IClassFactory
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *pUnkOuter, REFIID riid,
                                           void **ppvObject) override {
    if (pUnkOuter)
      return CLASS_E_NOAGGREGATION;
    CMyUIAProxy *pObj = new CMyUIAProxy();
    if (!pObj)
      return E_OUTOFMEMORY;
    HRESULT hr = pObj->QueryInterface(riid, ppvObject);
    pObj->Release();
    return hr;
  }

  HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override {
    if (fLock)
      g_refCount++;
    else
      g_refCount--;
    return S_OK;
  }
};

extern "C" BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_hInst = hInst;
    DisableThreadLibraryCalls(hInst);
  }
  return TRUE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI
DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv) {
  if (rclsid == CLSID_WineUIAExtension) {
    CClassFactory *pFactory = new CClassFactory();
    if (!pFactory)
      return E_OUTOFMEMORY;
    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
  }
  return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow() {
  return g_refCount == 0 ? S_OK : S_FALSE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllRegisterServer() {
  wchar_t modulePath[MAX_PATH];
  if (GetModuleFileNameW(g_hInst, modulePath, MAX_PATH) == 0) {
    return E_FAIL;
  }

  std::wstring clsidStr = GuidToString(CLSID_WineUIAExtension);
  std::wstring key = L"CLSID\\" + clsidStr;

  if (!SetRegistryKey(HKEY_CLASSES_ROOT, key, L"", L"Wine UIA Extension"))
    return E_FAIL;
  if (!SetRegistryKey(HKEY_CLASSES_ROOT, key + L"\\InProcServer32", L"",
                      modulePath))
    return E_FAIL;
  if (!SetRegistryKey(HKEY_CLASSES_ROOT, key + L"\\InProcServer32",
                      L"ThreadingModel", L"Both"))
    return E_FAIL;

  return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllUnregisterServer() {
  std::wstring clsidStr = GuidToString(CLSID_WineUIAExtension);
  std::wstring key = L"CLSID\\" + clsidStr;
  DeleteRegistryKey(HKEY_CLASSES_ROOT, key);
  return S_OK;
}
