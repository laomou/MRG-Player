#include "util/Download.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#if !defined(_WIN32)
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>

#include <cerrno>
#include <vector>

// posix_spawnp reads the caller's environment to search PATH for the fetcher.
extern char **environ;
#endif

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
DownloadStatus downloadFile(const std::string &url, const std::string &target, std::uint64_t maximumBytes,
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
    return DownloadStatus::Failed;
  }
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  HINTERNET session = WinHttpOpen(L"MRG Player", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (session == nullptr) {
    error = "Cannot start an HTTP session (error " + std::to_string(GetLastError()) + ")";
    return DownloadStatus::Network;
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
    return DownloadStatus::Network;
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
    // A 4xx means the file simply is not there; a 5xx means the service is
    // down, which is worth treating like an unreachable host.
    return ok && status < 500 ? DownloadStatus::NotFound : DownloadStatus::Network;
  }
  FILE *file = _wfopen(widen(target).c_str(), L"wb");
  if (file == nullptr) {
    error = "Cannot write the downloaded file";
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return DownloadStatus::Failed;
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
    return DownloadStatus::Network;
  }
  return written != 0 ? DownloadStatus::Ok : DownloadStatus::Failed;
}
#else
namespace {

struct ProgramResult {
  bool started = false;  // False when the program is not installed.
  int code = 0;          // Exit code, or 128 + signal when it was killed.
};

// Runs one program with its own argument vector. posix_spawnp searches PATH
// like a shell would, but nothing the caller passes is ever parsed as shell
// syntax, so a URL or a path cannot become a command. stderr goes to
// /dev/null: the fetcher's complaint is returned as a status instead of being
// mixed into the launcher's own output, which may be a serial console.
ProgramResult runProgram(const std::vector<std::string> &arguments) {
  std::vector<char *> argv;
  argv.reserve(arguments.size() + 1);
  for (const std::string &argument : arguments)
    argv.push_back(const_cast<char *>(argument.c_str()));
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  pid_t pid = 0;
  const int spawned = posix_spawnp(&pid, argv.front(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (spawned != 0)
    return {};
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR)
      return {true, 0};
  }
  return {true, WIFEXITED(status) ? WEXITSTATUS(status) : 128};
}

DownloadStatus classifyCurl(int code) {
  switch (code) {
  case 22:            // HTTP status >= 400: the file is not on the server.
    return DownloadStatus::NotFound;
  case 5:             // Could not resolve the proxy.
  case 6:             // Could not resolve the host.
  case 7:             // Could not connect.
  case 18:            // Partial transfer.
  case 28:            // Timed out.
  case 35:            // TLS handshake failed.
  case 52:            // Empty reply.
  case 55:            // Sending the request failed.
  case 56:            // Receiving the reply failed.
    return DownloadStatus::Network;
  default:
    return DownloadStatus::Failed;
  }
}

DownloadStatus classifyWget(int code) {
  switch (code) {
  case 4:             // Network failure.
  case 5:             // TLS verification failed.
  case 6:             // Authentication failure.
  case 7:             // Protocol error.
    return DownloadStatus::Network;
  case 8:             // The server answered with an error status.
    return DownloadStatus::NotFound;
  default:
    return DownloadStatus::Failed;
  }
}

}  // namespace

DownloadStatus downloadFile(const std::string &url, const std::string &target, std::uint64_t maximumBytes,
                            std::uint64_t &written, std::string &error) {
  written = 0;
  // The cap is applied twice: curl refuses a body larger than the limit before
  // it writes anything, and the result is checked afterwards because wget has
  // no equivalent option and older curls ignore it for chunked replies.
  std::vector<std::string> curl{"curl", "-fsSL", "--retry", "2", "--max-time", "120"};
  if (maximumBytes != 0) {
    curl.insert(curl.end(), {"--max-filesize", std::to_string(maximumBytes)});
  }
  curl.insert(curl.end(), {"-o", target, url});
  const ProgramResult curlResult = runProgram(curl);
  DownloadStatus status = DownloadStatus::Failed;
  if (curlResult.started)
    status = curlResult.code == 0 ? DownloadStatus::Ok : classifyCurl(curlResult.code);
  // Only reach for wget when curl is not installed. Once curl ran, a retry
  // through another client would repeat the same timeout for no new answer.
  if (!curlResult.started) {
    const ProgramResult wgetResult = runProgram({"wget", "-q", "-T", "60", "-O", target, url});
    if (!wgetResult.started) {
      error = "The download failed; install curl or wget, or fetch the file by hand";
      return DownloadStatus::Failed;
    }
    status = wgetResult.code == 0 ? DownloadStatus::Ok : classifyWget(wgetResult.code);
  }
  if (status != DownloadStatus::Ok) {
    error = status == DownloadStatus::Network      ? "Cannot reach the download server"
            : status == DownloadStatus::NotFound   ? "The download server does not have that file"
                                                    : "The download failed";
    return status;
  }
  std::error_code code;
  written = fs::file_size(paths::fromUtf8(target), code);
  if (code) {
    written = 0;
    error = "The downloaded file was not written";
    return DownloadStatus::Failed;
  }
  if (maximumBytes != 0 && written > maximumBytes) {
    std::error_code ignored;
    fs::remove(paths::fromUtf8(target), ignored);
    written = 0;
    error = "The download is larger than the allowed size (" + std::to_string(maximumBytes) + " bytes)";
    return DownloadStatus::Failed;
  }
  return DownloadStatus::Ok;
}
#endif

}  // namespace launcher::io
