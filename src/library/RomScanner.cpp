#include "library/RomScanner.hpp"

#include <algorithm>
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

bool isGameFile(const fs::path &path) {
  return game::platformFromExtension(paths::toUtf8(path)) !=
         game::GamePlatform::Unknown;
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

RomScanSummary importGames(const std::vector<std::string> &directories, GameLibrary &library) {
  RomScanSummary summary;
  for (const std::string &path : findGames(directories)) {
    ++summary.found;
    switch (library.importGame(path)) {
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
