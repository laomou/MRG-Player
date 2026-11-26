#pragma once

#include <string>
#include <vector>

namespace launcher::library {

class GameLibrary;

// What one pass over the ROM folders did. found counts the game files seen,
// which tells an empty folder apart from a folder whose games are all in the
// library already.
struct RomScanSummary {
  int found = 0;
  int imported = 0;
  int alreadyPresent = 0;
  int failed = 0;
};

// Game files under the folders, in a stable order. Dot entries are skipped,
// per-system subfolders such as Roms/nes/ are searched, and the walk stops at
// a fixed depth and file count so a card full of unrelated files cannot stall
// the launcher.
[[nodiscard]] std::vector<std::string> findGames(const std::vector<std::string> &directories);

// Imports every game found under the folders. Files the library already holds
// are counted rather than copied twice, because the import deduplicates by
// content hash.
RomScanSummary importGames(const std::vector<std::string> &directories, GameLibrary &library);

} // namespace launcher::library
