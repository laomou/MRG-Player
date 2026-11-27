#include "library/RomScanner.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>

#include "game/GamePlatform.hpp"
#include "library/GameLibrary.hpp"
#include "util/Utf8Path.hpp"

namespace launcher::library {
namespace {
namespace fs = std::filesystem;

// A handheld card holds thousands of files; the launcher only needs the ones
// that look like games. The depth covers Roms/<system>/<file> and a little
// nesting inside a user's own folders.
constexpr int kMaxDepth = 4;
constexpr std::size_t kMaxFiles = 2000;

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool isGameFile(const fs::path &path) {
  // The system folder decides when there is one, so a .bin inside Roms/MD is a
  // game while a README.md next to it is not.
  return game::detectPlatform(paths::toUtf8(path)) !=
         game::GamePlatform::Unknown;
}

// The tidy-up below only touches the top of a ROM tree, never a folder a player
// made inside it: Roms/Save States/ is left exactly where it is.
bool isRomsRoot(const fs::path &directory) {
  return lowercase(paths::toUtf8(directory.filename())) == "roms";
}

} // namespace

std::vector<std::string> findGames(const std::vector<std::string> &directories) {
  std::vector<std::string> games;
  for (const std::string &directory : directories) {
    if (games.size() >= kMaxFiles)
      break;
    std::error_code ec;
    const fs::path root = paths::fromUtf8(directory);
    if (!fs::is_directory(root, ec) || ec)
      continue;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
      if (it.depth() > kMaxDepth) {
        it.disable_recursion_pending();
        continue;
      }
      const fs::path entry = it->path();
      const std::string name = paths::toUtf8(entry.filename());
      // Dot directories hold save data, cover art and the managed library
      // itself; walking them would re-import the launcher's own copies.
      if (!name.empty() && name.front() == '.') {
        it.disable_recursion_pending();
        continue;
      }
      std::error_code fileError;
      if (!it->is_regular_file(fileError) || fileError)
        continue;
      if (!isGameFile(entry))
        continue;
      games.push_back(paths::toUtf8(entry));
      if (games.size() >= kMaxFiles)
        break;
    }
  }
  std::sort(games.begin(), games.end());
  return games;
}

std::string fileLooseRom(const std::string &path) {
  const fs::path source = paths::fromUtf8(path);
  if (!isRomsRoot(source.parent_path()))
    return path;
  const std::string system(
      game::platformDescriptor(game::detectPlatform(path)).cardDirectory);
  if (system.empty())
    return path;
  const fs::path destination = source.parent_path() / system / source.filename();
  std::error_code ec;
  // A file that is already named in the target folder wins: filing must never
  // cost a ROM, so the loose copy stays where it is for a human to look at.
  if (fs::exists(destination, ec))
    return path;
  fs::create_directories(destination.parent_path(), ec);
  if (ec)
    return path;
  fs::rename(source, destination, ec);
  if (ec)
    return path;
  return paths::toUtf8(destination);
}

RomScanSummary importGames(const std::vector<std::string> &directories, GameLibrary &library) {
  RomScanSummary summary;
  for (const std::string &path : findGames(directories)) {
    ++summary.found;
    // The tree a scan leaves behind is the tree a card has: a game dropped at
    // the top of Roms/ moves into its system folder before it is imported, so
    // the same folder reads the same way on a card and on a desktop.
    switch (library.importGame(fileLooseRom(path))) {
    case ImportStatus::Imported:
      ++summary.imported;
      break;
    case ImportStatus::AlreadyPresent:
      ++summary.alreadyPresent;
      break;
    case ImportStatus::Failed:
      ++summary.failed;
      break;
    }
  }
  return summary;
}

} // namespace launcher::library
