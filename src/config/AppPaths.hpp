#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace launcher::config {

[[nodiscard]] std::string dataRoot();
// Directory for one class of runtime data, for example "saves" and "nes". It is
// created if missing.
[[nodiscard]] std::string dataSubdirectory(std::string_view first, std::string_view second);

// Directory holding the executable, with the trailing separator SDL uses; the
// pak keeps its fonts and bundled ROMs next to the binary.
[[nodiscard]] std::string executableDirectory();

// Folders scanned for games when the platform has no file dialog: MRG_ROM_DIRS
// when the launcher is packaged for a device, otherwise a roms/ folder beside
// the managed library and next to the executable. Missing folders are dropped.
[[nodiscard]] std::vector<std::string> romSearchDirectories();

// Folders holding box art, one per ROM tree: Roms/ is paired with the Imgs/
// folder beside it, which is where the stock launcher keeps its artwork. They
// are returned whether or not they exist yet, because a downloaded cover is
// written there too, and the stock launcher and MRG Player then share one file.
[[nodiscard]] std::vector<std::string> coverDirectories();

// cover-names.json in the data root: the launcher title to No-Intro name
// mapping used by automatic lookups.
[[nodiscard]] std::string coverNamesPath();

} // namespace launcher::config
