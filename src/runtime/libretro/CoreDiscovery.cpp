#include "runtime/libretro/CoreDiscovery.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>

#include "SDL_filesystem.h"

#include "config/AppPaths.hpp"
#include "runtime/libretro/CoreArchive.hpp"
#include "util/Utf8Path.hpp"

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
// mGBA is the last entry on purpose: it runs GB and GBC as well, so a build
// can ship one well-licensed core instead of a second GPL-only binary.
constexpr const char *kGameBoyCores[] = {"gambatte_libretro", "sameboy_libretro", "gearboy_libretro",
                                         "mgba_libretro"};
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
  return paths::fromUtf8(root) / "cores-unpacked";
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
    paths.push_back(paths::toUtf8(paths::fromUtf8(root) / "cores"));
#if defined(_WIN32)
  // A portable Windows copy keeps its cores next to the executable.
  if (char *base = SDL_GetBasePath(); base != nullptr) {
    paths.push_back(paths::toUtf8(paths::fromUtf8(base) / "cores"));
    SDL_free(base);
  }
  return paths;
#else
  for (const char *systemDirectory : {"/usr/lib/libretro", "/usr/lib64/libretro", "/usr/lib/x86_64-linux-gnu/libretro",
                                      "/usr/local/lib/libretro"})
    paths.emplace_back(systemDirectory);
  if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    const fs::path localCoreDirectory =
        paths::fromUtf8(home) / ".local" / "lib" / "libretro";
    paths.push_back(paths::toUtf8(localCoreDirectory));
  }
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
      const fs::path path = paths::fromUtf8(directory) / candidate;
      if (fs::is_regular_file(path, error) && !error)
        return paths::toUtf8(path);
      if (unpackRoot.empty())
        continue;
      // The buildbot publishes <name>.so.zip, so accept an unpacked archive in
      // the same place the extracted library would have been.
      const fs::path archive =
          paths::fromUtf8(directory) / (candidate + ".zip");
      if (!fs::is_regular_file(archive, error) || error)
        continue;
      std::string extracted;
      std::string message;
      if (unpackCoreFromZip(paths::toUtf8(archive), candidate,
                            paths::toUtf8(unpackRoot), extracted, message))
        return extracted;
      std::fprintf(stderr, "core archive %s: %s\n",
                   paths::toUtf8(archive).c_str(), message.c_str());
    }
  }
  return {};
}

bool servedByCore(game::GamePlatform platform) noexcept {
  return platform != game::GamePlatform::Unknown && familyFor(platform) != nullptr;
}

bool removeInstalledCore(game::GamePlatform platform, std::string &error) {
  const std::string installed = findCore(platform);
  if (installed.empty()) {
    error = "no core is installed for this platform";
    return false;
  }
  const fs::path library = paths::fromUtf8(installed);
  std::error_code failure;
  if (!fs::remove(library, failure) || failure) {
    error = "cannot remove " + installed + ": " +
            (failure ? failure.message()
                     : std::string("the file is unchanged"));
    return false;
  }
  // The download archive and the copy unpacked from one belong to the same
  // installation, so they go too. Both are best effort: a leftover archive
  // never resurrects a core that is already gone.
  std::error_code ignored;
  const fs::path unpackRoot = unpackedCoreDirectory();
  if (!unpackRoot.empty())
    fs::remove(unpackRoot / library.filename(), ignored);
  const std::string archiveName = paths::toUtf8(library.filename()) + ".zip";
  fs::remove(library.parent_path() / archiveName, ignored);
  for (const std::string &directory : coreSearchPaths())
    fs::remove(paths::fromUtf8(directory) / archiveName, ignored);
  return true;
}

} // namespace launcher::runtime::libretro
