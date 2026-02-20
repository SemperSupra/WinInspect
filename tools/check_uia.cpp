#include <windows.h>
#include <uiautomation.h>
#include <iostream>
#include <string>

int main() {
    std::cout << "Initializing COM..." << std::endl;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "CoInitializeEx failed: " << std::hex << hr << std::endl;
        return 1;
    }

    std::cout << "Creating IUIAutomation instance..." << std::endl;
    IUIAutomation* pAutomation = NULL;
    hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, IID_IUIAutomation, (void**)&pAutomation);
    if (FAILED(hr)) {
        std::cerr << "CoCreateInstance CLSID_CUIAutomation failed: " << std::hex << hr << std::endl;
        CoUninitialize();
        return 1;
    }

    std::cout << "UIA initialized successfully." << std::endl;

    std::cout << "Getting Root Element..." << std::endl;
    IUIAutomationElement* pRoot = NULL;
    hr = pAutomation->GetRootElement(&pRoot);
    if (FAILED(hr) || !pRoot) {
        std::cerr << "GetRootElement failed: " << std::hex << hr << std::endl;
    } else {
        std::cout << "Got Root Element." << std::endl;
        // Try to get name?
        BSTR bName = NULL;
        if (SUCCEEDED(pRoot->get_CurrentName(&bName))) {
             std::wcout << L"Root Name: " << (bName ? bName : L"") << std::endl;
             SysFreeString(bName);
        }
        pRoot->Release();
    }

    pAutomation->Release();
    CoUninitialize();
    std::cout << "Done." << std::endl;
    return 0;
}
