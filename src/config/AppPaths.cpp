#include "config/AppPaths.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "util/Utf8Path.hpp"

namespace launcher::config {
namespace {
namespace fs = std::filesystem;

// The pak ships a list of folders; Windows uses ';' because ':' belongs to the
// drive letter.
#if defined(_WIN32)
constexpr char kPathListSeparator = ';';
#else
constexpr char kPathListSeparator = ':';
#endif

std::string preferenceRoot(const char *product) {
  char *path = SDL_GetPrefPath("MRG", product);
  if (path == nullptr)
    return {};
  std::string result(path);
  SDL_free(path);
  return result;
}

bool copyIfMissing(const fs::path &source, const fs::path &destination,
                   std::string &error) {
  std::error_code ec;
  if (!fs::exists(source, ec) || ec || fs::exists(destination, ec))
    return !ec;
  fs::create_directories(destination.parent_path(), ec);
  if (ec) {
    error = "Cannot create migrated data directory: " + ec.message();
    return false;
  }
  if (fs::is_directory(source, ec)) {
    fs::copy(source, destination,
             fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
  } else {
    fs::copy_file(source, destination, fs::copy_options::skip_existing, ec);
  }
  if (ec) {
    error = "Cannot migrate legacy launcher data: " + ec.message();
    return false;
  }
  return true;
}

} // namespace

std::string dataRoot() {
  // The handheld pak keeps user data on the SD card so replacing the pak on an
  // update never costs a save file. SDL_GetPrefPath knows nothing about that
  // layout, so an explicit override wins over the desktop default.
  if (const char *override = std::getenv("MRG_DATA_DIR");
      override != nullptr && override[0] != '\0') {
    std::string root(override);
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\'))
      root.pop_back();
    return root + '/';
  }
  return preferenceRoot("MRGPlayer");
}

std::string legacyDataRoot() { return preferenceRoot("NESPlayerLauncher"); }

bool migrateLegacyData(std::string &error) {
  error.clear();
  const fs::path current(paths::fromUtf8(dataRoot()));
  const fs::path legacy(paths::fromUtf8(legacyDataRoot()));
  if (current.empty()) {
    error = std::string("Cannot locate MRG Player data directory: ") +
            SDL_GetError();
    return false;
  }
  std::error_code ec;
  fs::create_directories(current, ec);
  if (ec) {
    error = "Cannot create MRG Player data directory: " + ec.message();
    return false;
  }
  if (legacy.empty() || !fs::exists(legacy, ec) || ec)
    return !ec;
  return copyIfMissing(legacy / "settings.ini", current / "settings.ini",
                       error) &&
         copyIfMissing(legacy / "roms", current / "library" / "nes", error) &&
         copyIfMissing(legacy / "saves", current / "saves" / "nes", error);
}

std::string executableDirectory() {
  char *base = SDL_GetBasePath();
  const std::string result = base != nullptr ? std::string(base) : std::string();
  SDL_free(base);
  return result;
}

std::vector<std::string> romSearchDirectories() {
  std::vector<std::string> directories;
  const auto add = [&directories](const std::string &value) {
    if (value.empty())
      return;
    std::error_code ec;
    const fs::path candidate = paths::fromUtf8(value);
    if (!fs::is_directory(candidate, ec) || ec)
      return;
    const std::string normalized = paths::toUtf8(candidate);
    if (std::find(directories.begin(), directories.end(), normalized) ==
        directories.end())
      directories.push_back(normalized);
  };

  // A pak or a device launcher names the folders directly: the bundled roms/
  // next to the executable, the card's own Roms tree, and whatever the user
  // added. Separated the way the host names a list; paths are absolute there,
  // and a relative entry is read as relative to the executable.
  if (const char *override = std::getenv("MRG_ROM_DIRS");
      override != nullptr && override[0] != '\0') {
    const std::string executablePath = executableDirectory();
    const std::string list(override);
    std::size_t begin = 0;
    while (begin <= list.size()) {
      const std::size_t end = list.find(kPathListSeparator, begin);
      std::string entry = list.substr(
          begin, end == std::string::npos ? std::string::npos : end - begin);
      if (!entry.empty() && !paths::fromUtf8(entry).is_absolute())
        entry = executablePath + entry;
      add(entry);
      if (end == std::string::npos)
        break;
      begin = end + 1;
    }
    return directories;
  }

  // Desktop default: a roms/ folder beside the managed library is enough, and
  // the same folder next to the executable covers a portable build.
  const std::string root = dataRoot();
  if (!root.empty())
    add(paths::toUtf8(paths::fromUtf8(root) / "roms"));
  add(executableDirectory() + "roms");
  return directories;
}

std::vector<std::string> coverSearchDirectories() {
  std::vector<std::string> directories;
  for (const std::string &roms : romSearchDirectories()) {
    // Roms/ maps to the Imgs/ beside it, which is where the stock launcher
    // keeps the box art of the games it shipped with.
    const fs::path candidate = paths::fromUtf8(roms).parent_path() / "Imgs";
    if (candidate.empty())
      continue;
    std::error_code ec;
    if (!fs::is_directory(candidate, ec) || ec)
      continue;
    const std::string normalized = paths::toUtf8(candidate);
    if (std::find(directories.begin(), directories.end(), normalized) ==
        directories.end())
      directories.push_back(normalized);
  }
  return directories;
}

std::string dataSubdirectory(std::string_view first, std::string_view second) {
  std::string migrationError;
  (void)migrateLegacyData(migrationError);
  const std::string root = dataRoot();
  const fs::path base = root.empty() ? fs::path(".") : paths::fromUtf8(root);
  const fs::path directory = base / std::string(first) / std::string(second);
  std::error_code ec;
  fs::create_directories(directory, ec);
  return paths::toUtf8(directory);
}

} // namespace launcher::config
