#include "util/Download.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#include "util/Utf8Path.hpp"

namespace launcher::io {
namespace {
namespace fs = std::filesystem;

#if defined(_WIN32)
std::wstring widen(const std::string &text) {
  if (text.empty())
    return std::wstring();
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), length);
  return wide;
}

std::string narrow(const std::wstring &text) {
  if (text.empty())
    return std::string();
  const int length =
      WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string narrow(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), narrow.data(), length, nullptr, nullptr);
  return narrow;
}
#endif

}  // namespace

#if defined(_WIN32)
bool downloadFile(const std::string &url, const std::string &target, std::uint64_t maximumBytes,
                  std::uint64_t &written, std::string &error) {
  written = 0;
  const std::wstring wideUrl = widen(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwSchemeLength = static_cast<DWORD>(-1);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  if (WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts) == FALSE) {
    error = "Cannot parse the download URL";
    return false;
  }
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  HINTERNET session = WinHttpOpen(L"MRG Player", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (session == nullptr) {
    error = "Cannot start an HTTP session (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
  HINTERNET request =
      connection != nullptr
          ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                               parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
          : nullptr;
  if (request == nullptr) {
    error = "Cannot reach " + narrow(host) + " (error " + std::to_string(GetLastError()) + ")";
    if (connection != nullptr)
      WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return false;
  }
  // The servers answer with redirects in front of some files, so follow them.
  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
  bool ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
            WinHttpReceiveResponse(request, nullptr) != FALSE;
  DWORD status = 0;
  DWORD statusSize = sizeof(status);
  if (ok)
    ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) != FALSE;
  if (!ok || status != 200) {
    error = ok ? "The download failed (HTTP status " + std::to_string(status) + ")"
               : "The download failed (error " + std::to_string(GetLastError()) + ")";
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return false;
  }
  FILE *file = _wfopen(widen(target).c_str(), L"wb");
  if (file == nullptr) {
    error = "Cannot write the downloaded file";
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return false;
  }
  std::uint8_t buffer[64 * 1024];
  bool wroteEverything = true;
  while (ok && written < maximumBytes) {
    DWORD read = 0;
    if (WinHttpReadData(request, buffer, static_cast<DWORD>(sizeof(buffer)), &read) == FALSE) {
      wroteEverything = false;
      break;
    }
    if (read == 0)
      break;
    if (std::fwrite(buffer, 1, read, file) != read) {
      wroteEverything = false;
      break;
    }
    written += read;
  }
  std::fclose(file);
  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  if (!wroteEverything) {
    error = "The download was interrupted (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  return written != 0;
}
#else
bool downloadFile(const std::string &url, const std::string &target, std::uint64_t maximumBytes,
                  std::uint64_t &written, std::string &error) {
  (void)maximumBytes;
  written = 0;
  const std::string quotedUrl = "'" + url + "'";
  const std::string targetArgument = "'" + target + "'";
  const std::string curl = "curl -fsSL --retry 2 --max-time 120 -o " + targetArgument + " " + quotedUrl;
  if (std::system(curl.c_str()) != 0) {
    const std::string wget = "wget -q -T 60 -O " + targetArgument + " " + quotedUrl;
    if (std::system(wget.c_str()) != 0) {
      error = "The download failed; install curl or wget, or fetch the file by hand";
      return false;
    }
  }
  std::error_code code;
  written = fs::file_size(paths::fromUtf8(target), code);
  if (code) {
    error = "The downloaded file was not written";
    return false;
  }
  return true;
}
#endif

}  // namespace launcher::io
