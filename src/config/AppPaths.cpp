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

// Directories are handed out with a trailing separator, the way SDL_GetPrefPath
// and SDL_GetBasePath spell them.
std::string withTrailingSeparator(std::string value) {
  while (value.size() > 1 && (value.back() == '/' || value.back() == '\\'))
    value.pop_back();
  return value + '/';
}

std::string normalized(const std::string &value) {
  return paths::toUtf8(paths::fromUtf8(value));
}

// The folders the launcher reads, before the existence filter: a cover is
// written beside a ROM tree the player has not created yet, so the tree has to
// be named even while it is missing.
std::vector<std::string> configuredRomDirectories() {
  std::vector<std::string> directories;
  const auto add = [&directories](const std::string &value) {
    if (value.empty())
      return;
    const std::string entry = normalized(value);
    if (std::find(directories.begin(), directories.end(), entry) ==
        directories.end())
      directories.push_back(entry);
  };

  // A pak or a device launcher names the folders directly: the card's own Roms
  // tree, and whatever the user added. Separated the way the host names a
  // list; paths are absolute there, and a relative entry is read as relative
  // to the executable.
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

bool insideDirectory(const std::string &path, const std::string &directory) {
  return !directory.empty() && path.size() > directory.size() &&
         path.compare(0, directory.size(), directory) == 0;
}

} // namespace

std::string dataRoot() {
  // The handheld pak keeps user data on the SD card so replacing the pak on an
  // update never costs a save file. SDL_GetPrefPath knows nothing about that
  // layout, so an explicit override wins over the desktop default.
  if (const char *override = std::getenv("MRG_DATA_DIR");
      override != nullptr && override[0] != '\0')
    return withTrailingSeparator(override);
  return preferenceRoot("MRGPlayer");
}

std::string cardRoot() {
  // One volume holds both halves of a card: the Roms/ and Imgs/ trees the stock
  // launcher reads, and the hidden folder the pak keeps its saves in. SDL knows
  // only the second one, so the pak names the first.
  if (const char *override = std::getenv("MRG_CARD_ROOT");
      override != nullptr && override[0] != '\0')
    return withTrailingSeparator(override);
  return dataRoot();
}

std::string executableDirectory() {
  char *base = SDL_GetBasePath();
  const std::string result = base != nullptr ? std::string(base) : std::string();
  SDL_free(base);
  return result;
}

std::string certificateBundlePath() {
  const std::string executable = executableDirectory();
  const std::string candidates[] = {
      executable + "assets/ca-certificates.crt",
      executable + "res/ca-certificates.crt",
      executable + "../share/mrg-player/ca-certificates.crt",
  };
  for (const std::string &candidate : candidates) {
    std::error_code ec;
    if (fs::is_regular_file(paths::fromUtf8(candidate), ec) && !ec)
      return candidate;
  }
  return {};
}

std::vector<std::string> romSearchDirectories() {
  std::vector<std::string> directories;
  for (const std::string &candidate : configuredRomDirectories()) {
    std::error_code ec;
    if (!fs::is_directory(paths::fromUtf8(candidate), ec) || ec)
      continue;
    directories.push_back(candidate);
  }
  return directories;
}

std::vector<std::string> coverDirectories() {
  std::vector<std::string> directories;
  const auto add = [&directories](const std::string &value) {
    if (value.empty())
      return;
    const std::string entry = normalized(value);
    if (std::find(directories.begin(), directories.end(), entry) ==
        directories.end())
      directories.push_back(entry);
  };

  // The card's own Imgs/ comes first, so a downloaded cover lands where the
  // stock launcher shows it and never inside the pak, whose folder an update
  // replaces wholesale. It is named even when it does not exist yet: writing
  // the first cover is what creates it.
  const std::string card = cardRoot();
  if (!card.empty())
    add(paths::toUtf8(paths::fromUtf8(card) / "Imgs"));

  const std::string executable = executableDirectory();
  for (const std::string &roms : configuredRomDirectories()) {
    // Art that travels with the binary belongs to the pak and leaves with it,
    // so it is not somewhere a download may be written.
    if (insideDirectory(roms, executable))
      continue;
    add(paths::toUtf8(paths::fromUtf8(roms).parent_path() / "Imgs"));
  }
  return directories;
}

std::string coverNamesPath() {
  // cover-names.json describes the library, so it sits with it instead of
  // crowding the root next to settings.ini.
  const std::string root = dataRoot();
  const fs::path base = root.empty() ? fs::path(".") : paths::fromUtf8(root);
  const fs::path path = base / "library" / "cover-names.json";
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  return paths::toUtf8(path);
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
