#pragma once

#include <string>

namespace launcher::runtime::libretro {

// The libretro buildbot ships every core as a one-file .zip, so a player who
// downloads one should not have to unpack it by hand. This pulls a single
// shared library out of such an archive. The entry is matched by base name, so a
// directory prefix inside the zip cannot place a file outside the destination.
// Builds without zlib report the missing dependency instead of failing quietly.
[[nodiscard]] bool unpackCoreFromZip(const std::string &zipPath, const std::string &expectedFileName,
                                     const std::string &destinationDirectory, std::string &extractedPath,
                                     std::string &error);

} // namespace launcher::runtime::libretro
