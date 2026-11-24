#include "runtime/libretro/CoreInstaller.hpp"

#include <cstdint>
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

#include "runtime/libretro/CoreArchive.hpp"
#include "runtime/libretro/CoreDiscovery.hpp"
#include "util/Sha256.hpp"
#include "util/Utf8Path.hpp"

namespace launcher::runtime::libretro {
namespace {
namespace fs = std::filesystem;

// A core archive is a few hundred kilobytes; anything far larger is not what
// the buildbot serves and is refused before it lands on disk.
constexpr std::uint64_t kMaximumDownloadBytes = 64U * 1024U * 1024U;

// The buildbot encodes the target platform in the directory name, next to the
// library suffix the core is built as.
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
constexpr const char *kDefaultBaseUrl = "https://buildbot.libretro.com/nightly/windows/x86_64/latest/";
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(_M_ARM64))
constexpr const char *kDefaultBaseUrl = "https://buildbot.libretro.com/nightly/apple/osx/arm64/latest/";
#elif defined(__APPLE__) && defined(__x86_64__)
constexpr const char *kDefaultBaseUrl = "https://buildbot.libretro.com/nightly/apple/osx/x86_64/latest/";
#elif defined(__linux__) && defined(__aarch64__)
constexpr const char *kDefaultBaseUrl = "https://buildbot.libretro.com/nightly/linux/aarch64/latest/";
#elif defined(__linux__) && defined(__x86_64__)
constexpr const char *kDefaultBaseUrl = "https://buildbot.libretro.com/nightly/linux/x86_64/latest/";
#else
constexpr const char *kDefaultBaseUrl = "";
#endif

// Preferred core per platform, most suitable first.
const std::vector<CorePackage> kPackages = {
    {game::GamePlatform::Nes, "mesen_libretro"},
    {game::GamePlatform::Nes, "nestopia_libretro"},
    {game::GamePlatform::Nes, "fceumm_libretro"},
    {game::GamePlatform::Nes, "quicknes_libretro"},
    {game::GamePlatform::Gba, "mgba_libretro"},
    {game::GamePlatform::Gba, "gpsp_libretro"},
    {game::GamePlatform::Gba, "vbam_libretro"},
    {game::GamePlatform::Gba, "vba_next_libretro"},
    {game::GamePlatform::Snes, "snes9x_libretro"},
    {game::GamePlatform::Snes, "bsnes_libretro"},
    {game::GamePlatform::Snes, "mesen-s_libretro"},
    {game::GamePlatform::GameBoy, "gambatte_libretro"},
    {game::GamePlatform::GameBoy, "sameboy_libretro"},
    {game::GamePlatform::GameBoy, "gearboy_libretro"},
    {game::GamePlatform::Genesis, "genesis_plus_gx_libretro"},
    {game::GamePlatform::Genesis, "picodrive_libretro"},
};

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

// SDL2 has no HTTP client, so the download goes through WinHTTP, which keeps the
// launcher free of an external curl.exe and works on a stock Windows install.
bool downloadFile(const std::string &url, const std::string &target, std::uint64_t &written, std::string &error) {
  written = 0;
  const std::wstring wideUrl = widen(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwSchemeLength = static_cast<DWORD>(-1);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  if (WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts) == FALSE) {
    error = "Cannot parse the core download URL";
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
  // The buildbot answers with redirects in front of some archives, so follow them.
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
    error = ok ? "The core download failed (HTTP status " + std::to_string(status) + ")"
               : "The core download failed (error " + std::to_string(GetLastError()) + ")";
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return false;
  }
  FILE *file = _wfopen(widen(target).c_str(), L"wb");
  if (file == nullptr) {
    error = "Cannot write the downloaded core";
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return false;
  }
  std::uint8_t buffer[64 * 1024];
  bool wroteEverything = true;
  while (ok && written < kMaximumDownloadBytes) {
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
    error = "The core download was interrupted (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  return written != 0;
}
#else
bool downloadFile(const std::string &url, const std::string &target, std::uint64_t &written, std::string &error) {
  written = 0;
  const std::string quotedUrl = "'" + url + "'";
  const std::string targetArgument = "'" + target + "'";
  const std::string curl = "curl -fsSL --retry 2 --max-time 120 -o " + targetArgument + " " + quotedUrl;
  if (std::system(curl.c_str()) != 0) {
    const std::string wget = "wget -q -T 60 -O " + targetArgument + " " + quotedUrl;
    if (std::system(wget.c_str()) != 0) {
      error = "The download failed; install curl or wget, or fetch the core by hand";
      return false;
    }
  }
  std::error_code code;
  written = fs::file_size(paths::fromUtf8(target), code);
  if (code) {
    error = "The downloaded core was not written";
    return false;
  }
  return true;
}
#endif

} // namespace

const std::vector<CorePackage> &corePackages() { return kPackages; }

std::string coreArchiveBaseUrl() {
  if (const char *custom = std::getenv("MRG_CORES_BASE_URL"); custom != nullptr && custom[0] != '\0') {
    std::string value = custom;
    if (value.back() != '/')
      value += '/';
    return value;
  }
  return kDefaultBaseUrl;
}

std::string coreArchiveUrl(const CorePackage &package) {
  return coreArchiveBaseUrl() + package.core + coreLibrarySuffix() + ".zip";
}

std::string coreLibraryName(const CorePackage &package) {
  return std::string(package.core) + coreLibrarySuffix();
}

InstallOutcome installCore(const CorePackage &package, const std::string &coresDirectory) {
  InstallOutcome outcome;
  if (coreArchiveBaseUrl().empty()) {
    outcome.message = "No libretro core downloads are configured for this platform; install the core by hand";
    return outcome;
  }
  std::error_code code;
  fs::create_directories(paths::fromUtf8(coresDirectory), code);
  if (code) {
    outcome.message = "Cannot create " + coresDirectory;
    return outcome;
  }
  const std::string library = coreLibraryName(package);
  const fs::path archive = paths::fromUtf8(coresDirectory) / (library + ".zip");
  fs::remove(archive, code);
  std::uint64_t bytes = 0;
  std::string downloadError;
  if (!downloadFile(coreArchiveUrl(package), paths::toUtf8(archive), bytes,
                    downloadError)) {
    fs::remove(archive, code);
    outcome.message = downloadError;
    return outcome;
  }
  if (bytes == 0 || bytes > kMaximumDownloadBytes) {
    fs::remove(archive, code);
    outcome.message = "The downloaded archive is empty or unexpectedly large (" + std::to_string(bytes) + " bytes)";
    return outcome;
  }
  std::fprintf(stderr, "core %s: %llu bytes, sha256 %s\n", package.core, static_cast<unsigned long long>(bytes),
               util::sha256File(paths::toUtf8(archive)).c_str());
  std::string extracted;
  std::string unpackError;
  const bool unpacked = unpackCoreFromZip(paths::toUtf8(archive), library,
                                          coresDirectory, extracted, unpackError);
  fs::remove(archive, code);
  if (!unpacked) {
    outcome.message = unpackError;
    return outcome;
  }
  outcome.ok = true;
  outcome.message = extracted;
  return outcome;
}

} // namespace launcher::runtime::libretro
