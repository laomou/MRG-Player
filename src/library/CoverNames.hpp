#pragma once

#include <map>
#include <string>
#include <vector>

#include "game/GamePlatform.hpp"

namespace launcher::library {

// The contents of cover-names.json: a launcher title mapped to the No-Intro
// name the thumbnail repositories file the art under.
using CoverNames = std::map<std::string, std::string>;

// Parses the flat JSON object. Keys and values must both be strings.
[[nodiscard]] bool parseCoverNames(const std::string &json, CoverNames &names, std::string &error);

// The file written once, when cover-names.json does not exist yet. It holds
// a few examples the player can replace; the launcher never writes it again.
[[nodiscard]] const char *coverNamesTemplate() noexcept;

// The libretro-thumbnails repository for a platform, empty when the platform
// has no known box art collection.
[[nodiscard]] std::string coverRepository(game::GamePlatform platform);

// Titles to try, in order: the mapped name (when present), the title itself,
// then the title with the common No-Intro region suffixes. Duplicates are
// dropped, so a mapping that equals the title costs nothing.
[[nodiscard]] std::vector<std::string> coverNameCandidates(const std::string &title, const CoverNames &names);

// Box art URLs for a title, most specific first. A mapped value that is
// already an http(s) URL is used verbatim, which lets a player point at a
// mirror without waiting for a launcher release.
[[nodiscard]] std::vector<std::string> coverUrls(game::GamePlatform platform, const std::string &title,
                                                const CoverNames &names);

// Percent-encodes everything outside the RFC 3986 unreserved set, so titles
// with spaces, parentheses, or Chinese characters survive the URL.
[[nodiscard]] std::string urlEncode(const std::string &value);

// The file name a cover is stored under. The stock launcher names box art after
// the ROM, so the title is kept as it is and only the characters a FAT card or
// Windows refuses are replaced.
[[nodiscard]] std::string coverFileName(const std::string &title);

// Where the cover for a title lives under one Imgs/ root:
// Imgs/<system>/<title>.png. Empty when the platform has no card folder.
[[nodiscard]] std::string coverTargetPath(const std::string &root, game::GamePlatform platform,
                                          const std::string &title);

}  // namespace launcher::library
