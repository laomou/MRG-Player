#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace launcher::game {

// A platform is a console family rather than a specific emulator: the built-in
// core serves the NES, and the libretro back end serves the rest. Values are
// persisted in the library metadata as integers, so new platforms may only be
// appended.
enum class GamePlatform { Unknown, Nes, Gba, Snes, GameBoy, Genesis };

struct PlatformDescriptor {
  GamePlatform platform = GamePlatform::Unknown;
  std::string_view id;
  std::string_view displayName;
  std::string_view fileTypeLabel;
  std::string_view managedDirectory;
  // The folder a card uses for this system, as the stock launcher names it, or
  // empty when it has none.
  std::string_view cardDirectory;
};

[[nodiscard]] const PlatformDescriptor &platformDescriptor(GamePlatform platform) noexcept;
[[nodiscard]] GamePlatform platformFromExtension(const std::string &path) noexcept;
// The platform the enclosing folders name, or Unknown when they name none.
[[nodiscard]] GamePlatform platformFromDirectory(const std::string &path) noexcept;
// The platform of a file: its system folder first, its extension second, and
// Unknown for the documentation a ROM set ships beside its games.
[[nodiscard]] GamePlatform detectPlatform(const std::string &path) noexcept;
[[nodiscard]] bool validateGameFile(GamePlatform platform, const std::string &path, std::string &error);
// Every file extension the importers accept, without the leading dot.
[[nodiscard]] std::vector<std::string> gameExtensions();

} // namespace launcher::game
