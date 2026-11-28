#include "runtime/libretro/CoreInstaller.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>


#include "config/AppPaths.hpp"
#include "runtime/libretro/CoreArchive.hpp"
#include "runtime/libretro/CoreDiscovery.hpp"
#if defined(MRG_ENABLE_CORE_DOWNLOAD) && MRG_ENABLE_CORE_DOWNLOAD
#include "util/Download.hpp"
#include "util/Sha256.hpp"
#endif
#include "util/Utf8Path.hpp"

namespace launcher::runtime::libretro {
namespace {
namespace fs = std::filesystem;

// A core archive is a few hundred kilobytes; anything far larger is not what
// the buildbot serves and is refused before it lands on disk.
#if defined(MRG_ENABLE_CORE_DOWNLOAD) && MRG_ENABLE_CORE_DOWNLOAD
constexpr std::uint64_t kMaximumDownloadBytes = 64U * 1024U * 1024U;
#endif

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
    // mGBA also runs GB/GBC, so a build that ships only mGBA still works.
    {game::GamePlatform::GameBoy, "mgba_libretro"},
    {game::GamePlatform::Genesis, "genesis_plus_gx_libretro"},
    {game::GamePlatform::Genesis, "picodrive_libretro"},
};


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

#if !defined(MRG_ENABLE_CORE_DOWNLOAD) || !MRG_ENABLE_CORE_DOWNLOAD
// The device build ships every core it needs inside the pak, so the network
// path is compiled out instead of failing at the end of a download.
InstallOutcome installCore(const CorePackage &package, const std::string &coresDirectory) {
  (void)package;
  (void)coresDirectory;
  InstallOutcome outcome;
  outcome.message = "This build ships its cores; runtime downloads are disabled";
  return outcome;
}
#else
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
  if (io::downloadFile(coreArchiveUrl(package), paths::toUtf8(archive), kMaximumDownloadBytes,
                       bytes, downloadError,
                       config::certificateBundlePath()) != io::DownloadStatus::Ok) {
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
#endif

} // namespace launcher::runtime::libretro
