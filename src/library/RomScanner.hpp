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
// per-system subfolders such as Roms/FC/ are searched and name the platform of
// what they hold, documentation shipped with a ROM set is left out, and the
// walk stops at a fixed depth and file count so a card full of unrelated files
// cannot stall the launcher.
[[nodiscard]] std::vector<std::string> findGames(const std::vector<std::string> &directories);

// Files a game dropped at the top of a Roms/ tree into the folder the card uses
// for its platform (Roms/FC, Roms/SFC, Roms/MD, Roms/GB, Roms/GBA) and returns
// where it ended up. A file already in a system folder, a folder of the player's
// own, an unknown format, or a name that already exists in the target is left
// alone and returned unchanged.
[[nodiscard]] std::string fileLooseRom(const std::string &path);

// Imports every game found under the folders. Files the library already holds
// are counted rather than copied twice, because the import deduplicates by
// content hash.
RomScanSummary importGames(const std::vector<std::string> &directories, GameLibrary &library);

} // namespace launcher::library
