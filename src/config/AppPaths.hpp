#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace launcher::config {

[[nodiscard]] std::string dataRoot();
// Directory for one class of runtime data, for example "saves" and "FC". It is
// created if missing.
[[nodiscard]] std::string dataSubdirectory(std::string_view first, std::string_view second);

// Root of the volume the games live on: the card on a device, the data root on
// a desktop. MRG_CARD_ROOT names it explicitly, because SDL_GetPrefPath knows
// only the data root and a device keeps that one in a hidden folder.
[[nodiscard]] std::string cardRoot();

// Directory holding the executable, with the trailing separator SDL uses; the
// pak keeps its fonts and cores next to the binary.
[[nodiscard]] std::string executableDirectory();

// CA bundle used for HTTPS downloads. Empty when the build has none: a desktop
// normally has a system store, while the handheld firmware ships no certificates
// at all, so the pak carries one next to its fonts.
[[nodiscard]] std::string certificateBundlePath();

// Folders scanned for games: MRG_ROM_DIRS when the launcher is packaged for a
// device, otherwise Roms/ under the data root and next to the executable.
// Missing folders are dropped.
[[nodiscard]] std::vector<std::string> romSearchDirectories();

// Folders holding box art, one per ROM tree: Roms/ is paired with the Imgs/
// folder beside it, which is where the stock launcher keeps its artwork. The
// card's own Imgs/ comes first, so a download lands on the volume that holds
// the games instead of inside the pak. A folder is returned whether or not it
// exists yet, because writing the first cover is what creates it.
[[nodiscard]] std::vector<std::string> coverDirectories();

// library/cover-names.json: the launcher title to No-Intro name mapping used by
// automatic lookups. It describes the library, so it sits beside it rather than
// in the root next to settings.ini.
[[nodiscard]] std::string coverNamesPath();

} // namespace launcher::config
