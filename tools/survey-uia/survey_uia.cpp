#include <UIAutomation.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <windows.h>

// Helper to escape JSON strings
std::string json_escape(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (c == '"')
      out += "\\\"";
    else if (c == '\\')
      out += "\\\\";
    else if (c == '\b')
      out += "\\b";
    else if (c == '\f')
      out += "\\f";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else if ((unsigned char)c < 0x20) {
      char buf[7];
      sprintf(buf, "\\u%04x", c);
      out += buf;
    } else
      out += c;
  }
  return out;
}

std::string w2u8(const std::wstring &ws) {
  if (ws.empty())
    return {};
  int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr,
                                0, nullptr, nullptr);
  std::string out(len, '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), len,
                      nullptr, nullptr);
  return out;
}

std::string bstr_to_utf8(BSTR bstr) {
  if (!bstr)
    return {};
  std::wstring ws(bstr, SysStringLen(bstr));
  return w2u8(ws);
}

struct CheckResult {
  std::string name;
  bool passed;
  std::string details;
};

std::vector<CheckResult> results;

void add_result(const std::string &name, bool passed,
                const std::string &details = "") {
  results.push_back({name, passed, details});
}

int main() {
  CoInitializeEx(NULL, COINIT_MULTITHREADED);

  IUIAutomation *pAutomation = NULL;
  HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                                IID_IUIAutomation, (void **)&pAutomation);

  add_result("CoCreateInstance(CLSID_CUIAutomation)", SUCCEEDED(hr),
             SUCCEEDED(hr) ? "" : "HRESULT: " + std::to_string(hr));

  if (SUCCEEDED(hr) && pAutomation) {
    IUIAutomationElement *pRoot = NULL;
    hr = pAutomation->GetRootElement(&pRoot);
    add_result("GetRootElement", SUCCEEDED(hr) && pRoot,
               SUCCEEDED(hr) ? "" : "HRESULT: " + std::to_string(hr));

    if (pRoot) {
      BSTR name = NULL;
      pRoot->get_CurrentName(&name);
      add_result("Root.CurrentName", name != NULL,
                 name ? bstr_to_utf8(name) : "NULL");
      SysFreeString(name);

      // Test FindAll Children
      IUIAutomationCondition *pTrueCondition = NULL;
      pAutomation->CreateTrueCondition(&pTrueCondition);
      if (pTrueCondition) {
        IUIAutomationElementArray *pChildren = NULL;
        hr = pRoot->FindAll(TreeScope_Children, pTrueCondition, &pChildren);
        add_result("Root.FindAll(Children)", SUCCEEDED(hr),
                   SUCCEEDED(hr) ? "" : "HRESULT: " + std::to_string(hr));

        if (SUCCEEDED(hr) && pChildren) {
          int count = 0;
          pChildren->get_Length(&count);
          add_result("Root.Children.Count", true, std::to_string(count));

          if (count > 0) {
            IUIAutomationElement *pChild = NULL;
            pChildren->GetElement(0, &pChild);
            if (pChild) {
              BSTR childName = NULL;
              pChild->get_CurrentName(&childName);
              add_result("Child[0].CurrentName", true,
                         childName ? bstr_to_utf8(childName) : "(null)");
              SysFreeString(childName);

              // Check Patterns
              IUnknown *pPattern = NULL;
              hr = pChild->GetCurrentPattern(UIA_LegacyIAccessiblePatternId,
                                             &pPattern);
              add_result("Child[0].LegacyIAccessiblePattern",
                         SUCCEEDED(hr) && pPattern, "");
              if (pPattern)
                pPattern->Release();

              pChild->Release();
            }
          }
          pChildren->Release();
        }
        pTrueCondition->Release();
      }
      pRoot->Release();
    }

    // Test ElementFromHandle
    HWND hDesktop = GetDesktopWindow();
    IUIAutomationElement *pFromHandle = NULL;
    hr = pAutomation->ElementFromHandle(hDesktop, &pFromHandle);
    add_result("ElementFromHandle(Desktop)", SUCCEEDED(hr) && pFromHandle, "");
    if (pFromHandle)
      pFromHandle->Release();

    pAutomation->Release();
  }

  CoUninitialize();

  // Output JSON
  std::cout << "{" << std::endl;
  std::cout << "  \"results\": [" << std::endl;
  for (size_t i = 0; i < results.size(); ++i) {
    std::cout << "    {" << std::endl;
    std::cout << "      \"name\": \"" << json_escape(results[i].name) << "\","
              << std::endl;
    std::cout << "      \"passed\": " << (results[i].passed ? "true" : "false")
              << "," << std::endl;
    std::cout << "      \"details\": \"" << json_escape(results[i].details)
              << "\"" << std::endl;
    std::cout << "    }" << (i < results.size() - 1 ? "," : "") << std::endl;
  }
  std::cout << "  ]" << std::endl;
  std::cout << "}" << std::endl;

  return 0;
}
