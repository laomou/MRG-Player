#pragma once

#include <map>
#include <string>
#include <vector>

#include "game/GamePlatform.hpp"

namespace launcher::library {

// The contents of covers/names.json: a launcher title mapped to the No-Intro
// name the thumbnail repositories file the art under.
using CoverNames = std::map<std::string, std::string>;

// Parses the flat JSON object. Keys and values must both be strings.
[[nodiscard]] bool parseCoverNames(const std::string &json, CoverNames &names, std::string &error);

// The file written once, when covers/names.json does not exist yet. It holds
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

}  // namespace launcher::library
