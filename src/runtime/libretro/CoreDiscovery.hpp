#pragma once

#include <string>
#include <vector>

#include "game/GamePlatform.hpp"

namespace launcher::runtime::libretro {

// Directories searched for libretro cores, highest priority first. The
// MRG_CORES_DIR environment variable comes first so a local build or a test can
// point at its own core without touching the system directories.
[[nodiscard]] std::vector<std::string> coreSearchPaths();
// Suffix cores are built as on this platform (.so, .dylib, .dll).
[[nodiscard]] const char *coreLibrarySuffix() noexcept;
// Library file names tried for a platform, most preferred first.
[[nodiscard]] std::vector<std::string> coreCandidates(game::GamePlatform platform);
// Full path of the preferred core on disk, empty when none is installed.
[[nodiscard]] std::string findCore(game::GamePlatform platform);
// True when a libretro core, rather than a built-in runtime, serves the platform.
[[nodiscard]] bool servedByCore(game::GamePlatform platform) noexcept;
// Deletes the installed core for a platform together with the archive it came
// from and any copy unpacked from one, so the platform goes back to being
// uninstalled. Returns false and fills error when nothing could be removed.
[[nodiscard]] bool removeInstalledCore(game::GamePlatform platform,
                                     std::string &error);

} // namespace launcher::runtime::libretro
