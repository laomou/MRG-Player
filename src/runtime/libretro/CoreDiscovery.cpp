#include "runtime/libretro/CoreDiscovery.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>

#include "SDL_filesystem.h"

#include "config/AppPaths.hpp"
#include "runtime/libretro/CoreArchive.hpp"

namespace launcher::runtime::libretro {
namespace {
namespace fs = std::filesystem;

#if defined(__APPLE__)
constexpr const char *kLibrarySuffix = ".dylib";
#elif defined(_WIN32)
constexpr const char *kLibrarySuffix = ".dll";
#else
constexpr const char *kLibrarySuffix = ".so";
#endif

// Names installed by the libretro buildbot and by distribution packages.
constexpr const char *kNesCores[] = {"mesen_libretro", "nestopia_libretro", "fceumm_libretro", "quicknes_libretro"};
constexpr const char *kGbaCores[] = {"mgba_libretro", "gpsp_libretro", "vbam_libretro", "vba_next_libretro"};
constexpr const char *kSnesCores[] = {"snes9x_libretro", "bsnes_libretro", "mesen-s_libretro"};
constexpr const char *kGameBoyCores[] = {"gambatte_libretro", "sameboy_libretro", "gearboy_libretro"};
constexpr const char *kGenesisCores[] = {"genesis_plus_gx_libretro", "picodrive_libretro"};

struct CoreFamily {
  game::GamePlatform platform;
  const char *const *names;
  std::size_t count;
};

constexpr CoreFamily kFamilies[] = {
    {game::GamePlatform::Nes, kNesCores, std::size(kNesCores)},
    {game::GamePlatform::Gba, kGbaCores, std::size(kGbaCores)},
    {game::GamePlatform::Snes, kSnesCores, std::size(kSnesCores)},
    {game::GamePlatform::GameBoy, kGameBoyCores, std::size(kGameBoyCores)},
    {game::GamePlatform::Genesis, kGenesisCores, std::size(kGenesisCores)},
};

const CoreFamily *familyFor(game::GamePlatform platform) {
  for (const CoreFamily &family : kFamilies) {
    if (family.platform == platform)
      return &family;
  }
  return nullptr;
}

// Downloaded cores arrive as <name>.so.zip and are unpacked once into the data
// directory, which keeps read-only system directories untouched.
fs::path unpackedCoreDirectory() {
  const std::string root = config::dataRoot();
  if (root.empty())
    return {};
  return fs::path(root) / "cores-unpacked";
}

} // namespace

std::vector<std::string> coreSearchPaths() {
  std::vector<std::string> paths;
  // An explicit directory is authoritative: it keeps the test suite hermetic and
  // lets a user pin one build of a core without the system copies winning.
  if (const char *custom = std::getenv("MRG_CORES_DIR"); custom != nullptr && custom[0] != '\0')
    return {custom};
  const std::string root = config::dataRoot();
  if (!root.empty())
    paths.push_back((fs::path(root) / "cores").string());
#if defined(_WIN32)
  // A portable Windows copy keeps its cores next to the executable.
  if (char *base = SDL_GetBasePath(); base != nullptr) {
    paths.push_back((fs::path(base) / "cores").string());
    SDL_free(base);
  }
  return paths;
#else
  for (const char *systemDirectory : {"/usr/lib/libretro", "/usr/lib64/libretro", "/usr/lib/x86_64-linux-gnu/libretro",
                                      "/usr/local/lib/libretro"})
    paths.emplace_back(systemDirectory);
  if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
    paths.push_back((fs::path(home) / ".local" / "lib" / "libretro").string());
  return paths;
#endif
}

const char *coreLibrarySuffix() noexcept { return kLibrarySuffix; }

std::vector<std::string> coreCandidates(game::GamePlatform platform) {
  std::vector<std::string> candidates;
  const CoreFamily *family = familyFor(platform);
  if (family == nullptr)
    return candidates;
  for (std::size_t index = 0; index < family->count; ++index)
    candidates.emplace_back(std::string(family->names[index]) + kLibrarySuffix);
  return candidates;
}

std::string findCore(game::GamePlatform platform) {
  const std::vector<std::string> candidates = coreCandidates(platform);
  if (candidates.empty())
    return {};
  const fs::path unpackRoot = unpackedCoreDirectory();
  for (const std::string &directory : coreSearchPaths()) {
    for (const std::string &candidate : candidates) {
      std::error_code error;
      const fs::path path = fs::path(directory) / candidate;
      if (fs::is_regular_file(path, error) && !error)
        return path.string();
      if (unpackRoot.empty())
        continue;
      // The buildbot publishes <name>.so.zip, so accept an unpacked archive in
      // the same place the extracted library would have been.
      const fs::path archive = fs::path(directory) / (candidate + ".zip");
      if (!fs::is_regular_file(archive, error) || error)
        continue;
      std::string extracted;
      std::string message;
      if (unpackCoreFromZip(archive.string(), candidate, unpackRoot.string(), extracted, message))
        return extracted;
      std::fprintf(stderr, "core archive %s: %s\n", archive.string().c_str(), message.c_str());
    }
  }
  return {};
}

bool servedByCore(game::GamePlatform platform) noexcept {
  return platform != game::GamePlatform::Unknown && familyFor(platform) != nullptr;
}

} // namespace launcher::runtime::libretro
