// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Mark E. DeYoung

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <vector>
#include <cstdlib>

#include "wininspect/update.hpp"
#include "wininspect/tinyjson.hpp"

#pragma comment(lib, "winhttp.lib")

namespace wininspect::update {

std::vector<int> parse_version(const std::string &tag) {
  std::vector<int> parts;
  std::string s = tag;

  // Strip leading 'v' or 'V'
  if (!s.empty() && (s[0] == 'v' || s[0] == 'V'))
    s = s.substr(1);

  std::istringstream ss(s);
  std::string segment;
  while (std::getline(ss, segment, '.')) {
    try {
      parts.push_back(std::stoi(segment));
    } catch (...) {
      parts.clear();
      return parts;
    }
  }
  return parts;
}

int compare_versions(const std::vector<int> &a, const std::vector<int> &b) {
  size_t n = (a.size() > b.size()) ? a.size() : b.size();
  for (size_t i = 0; i < n; ++i) {
    int va = (i < a.size()) ? a[i] : 0;
    int vb = (i < b.size()) ? b[i] : 0;
    if (va < vb) return -1;
    if (va > vb) return 1;
  }
  return 0;
}

// Helper: make an HTTPS GET request and return the response body
static std::string https_get(const std::wstring &host, const std::wstring &path) {
  std::string body;

  HINTERNET hSession = WinHttpOpen(
      L"WinInspect/1.0",
      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) return {};

  HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConnect) {
    WinHttpCloseHandle(hSession);
    return {};
  }

  HINTERNET hRequest = WinHttpOpenRequest(
      hConnect, L"GET", path.c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      WINHTTP_FLAG_SECURE);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return {};
  }

  // GitHub API requires a User-Agent
  const wchar_t *headers = L"User-Agent: WinInspect\r\nAccept: application/vnd.github+json\r\n";
  BOOL ok = WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (!ok || !WinHttpReceiveResponse(hRequest, nullptr)) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return {};
  }

  DWORD bytesRead = 0;
  char buf[4096];
  while (WinHttpReadData(hRequest, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
    buf[bytesRead] = '\0';
    body += buf;
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return body;
}

UpdateInfo check_for_update(const std::string &current_version) {
  UpdateInfo info;
  info.current_version = current_version;

  std::string json_body = https_get(L"api.github.com", L"/repos/SemperSupra/WinInspect/releases/latest");
  if (json_body.empty()) {
    info.error = "Failed to query GitHub API";
    return info;
  }

  try {
    auto root = json::parse(json_body);
    if (!root.is_obj()) {
      info.error = "Invalid API response";
      return info;
    }
    const auto &obj = root.as_obj();

    auto it_tag = obj.find("tag_name");
    if (it_tag == obj.end() || !it_tag->second.is_str()) {
      info.error = "No tag_name in response";
      return info;
    }
    info.latest_version = it_tag->second.as_str();

    auto it_body = obj.find("body");
    if (it_body != obj.end() && it_body->second.is_str())
      info.release_notes = it_body->second.as_str();

    // Scan assets array for installer and portable ZIP download URLs
    auto it_assets = obj.find("assets");
    if (it_assets != obj.end() && it_assets->second.is_arr()) {
      for (const auto &asset : it_assets->second.as_arr()) {
        if (!asset.is_obj()) continue;
        const auto &a = asset.as_obj();
        auto it_name = a.find("name");
        auto it_dl = a.find("browser_download_url");
        if (it_name == a.end() || it_dl == a.end()) continue;
        if (!it_name->second.is_str() || !it_dl->second.is_str()) continue;

        std::string name = it_name->second.as_str();
        if (name.find("Installer") != std::string::npos && info.installer_url.empty())
          info.installer_url = it_dl->second.as_str();
        else if (name.find("Portable") != std::string::npos && name.find(".zip") != std::string::npos && info.portable_zip_url.empty())
          info.portable_zip_url = it_dl->second.as_str();
      }
    }

    // Compare versions
    auto cur = parse_version(info.current_version);
    auto lat = parse_version(info.latest_version);
    if (cur.empty() || lat.empty()) {
      info.error = "Version parse error";
      return info;
    }
    info.update_available = (compare_versions(lat, cur) > 0);

  } catch (const std::exception &e) {
    info.error = std::string("JSON parse error: ") + e.what();
  }

  return info;
}

// Parse host and path from a URL
static bool parse_url(const std::string &url, std::string &host, std::string &path) {
  if (url.rfind("https://", 0) == 0) {
    std::string rest = url.substr(8);
    size_t slash = rest.find('/');
    if (slash == std::string::npos) { host = rest; path = "/"; }
    else { host = rest.substr(0, slash); path = rest.substr(slash); }
    return true;
  }
  if (url.rfind("http://", 0) == 0) {
    std::string rest = url.substr(7);
    size_t slash = rest.find('/');
    if (slash == std::string::npos) { host = rest; path = "/"; }
    else { host = rest.substr(0, slash); path = rest.substr(slash); }
    return true;
  }
  return false;
}

// Shared download logic: stream an HTTPS GET to a file
static bool download_to_file(const std::string &url, const std::wstring &outPath) {
  std::string host, path;
  if (!parse_url(url, host, path)) return false;

  std::wstring whost(host.begin(), host.end());
  std::wstring wpath(path.begin(), path.end());

  HINTERNET hSession = WinHttpOpen(
      L"WinInspect/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) return false;

  HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(),
                                       INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

  HINTERNET hRequest = WinHttpOpenRequest(
      hConnect, L"GET", wpath.c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
      WINHTTP_FLAG_SECURE);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
  }

  const wchar_t *headers = L"User-Agent: WinInspect\r\n";
  BOOL ok = WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (!ok || !WinHttpReceiveResponse(hRequest, nullptr)) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
  }

  // Follow redirects if needed
  DWORD statusCode = 0;
  DWORD statusCodeSize = sizeof(statusCode);
  WinHttpQueryHeaders(hRequest,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize,
                      WINHTTP_NO_HEADER_INDEX);
  if (statusCode == 302 || statusCode == 301) {
    wchar_t redirectUrl[2048];
    DWORD redirectSize = sizeof(redirectUrl);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            redirectUrl, &redirectSize, WINHTTP_NO_HEADER_INDEX)) {
      WinHttpCloseHandle(hRequest);
      WinHttpCloseHandle(hConnect);
      WinHttpCloseHandle(hSession);

      // Recursively follow redirect
      std::string redir(redirectUrl, redirectUrl + redirectSize / sizeof(wchar_t));
      return download_to_file(redir, outPath);
    }
  }

  HANDLE hFile = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
  }

  DWORD bytesRead = 0;
  char buf[8192];
  while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
    DWORD written = 0;
    WriteFile(hFile, buf, bytesRead, &written, nullptr);
  }

  CloseHandle(hFile);
  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return true;
}

std::string download_update(const std::string &url, const std::string &version_tag,
                            const std::string &type_hint) {
  if (url.empty()) return {};

  // Build output path in TEMP with appropriate extension
  wchar_t tempPath[MAX_PATH];
  if (!GetTempPathW(MAX_PATH, tempPath)) return {};

  std::wstring wtag(version_tag.begin(), version_tag.end());
  std::wstring outFile;
  if (type_hint == "portable") {
    outFile = std::wstring(tempPath) + L"WinInspectPortable-" + wtag + L".zip";
  } else {
    outFile = std::wstring(tempPath) + L"WinInspect-Installer-" + wtag + L".exe";
  }

  if (!download_to_file(url, outFile)) return {};

  // Return narrow path
  int len = WideCharToMultiByte(CP_UTF8, 0, outFile.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string result(len - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, outFile.c_str(), -1, &result[0], len, nullptr, nullptr);
  return result;
}

} // namespace wininspect::update
#endif
