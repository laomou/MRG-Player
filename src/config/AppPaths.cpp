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

  // Desktop default mirrors a card: Roms/<system> folders with Imgs/<system>
  // beside them, so a tree copied off a device opens the way it left it.
  const std::string root = dataRoot();
  if (!root.empty())
    add(paths::toUtf8(paths::fromUtf8(root) / "Roms"));
  const std::string executable = executableDirectory();
  add(executable + "Roms");
  add(executable + "roms");
  return directories;
}

std::vector<std::string> coverDirectories() {
  std::vector<std::string> directories;
  for (const std::string &roms : romSearchDirectories()) {
    // Roms/ maps to the Imgs/ beside it, which is where the stock launcher
    // keeps the box art of the games it shipped with. The folder is not
    // required to exist: a missing one is created when a cover is saved, so a
    // fresh card and a full one behave the same way.
    const fs::path candidate = paths::fromUtf8(roms).parent_path() / "Imgs";
    if (candidate.empty())
      continue;
    const std::string normalized = paths::toUtf8(candidate);
    if (std::find(directories.begin(), directories.end(), normalized) ==
        directories.end())
      directories.push_back(normalized);
  }
  return directories;
}

std::string coverNamesPath() {
  const std::string root = dataRoot();
  const fs::path base = root.empty() ? fs::path(".") : paths::fromUtf8(root);
  std::error_code ec;
  if (!root.empty())
    fs::create_directories(base, ec);
  return paths::toUtf8(base / "cover-names.json");
}

std::string dataSubdirectory(std::string_view first, std::string_view second) {
  const std::string root = dataRoot();
  const fs::path base = root.empty() ? fs::path(".") : paths::fromUtf8(root);
  const fs::path directory = base / std::string(first) / std::string(second);
  std::error_code ec;
  fs::create_directories(directory, ec);
  return paths::toUtf8(directory);
}

} // namespace launcher::config
