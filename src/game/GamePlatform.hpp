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
};

[[nodiscard]] const PlatformDescriptor &platformDescriptor(GamePlatform platform) noexcept;
[[nodiscard]] GamePlatform platformFromExtension(const std::string &path) noexcept;
[[nodiscard]] bool validateGameFile(GamePlatform platform, const std::string &path, std::string &error);
// Every file extension the importers accept, without the leading dot.
[[nodiscard]] std::vector<std::string> gameExtensions();

} // namespace launcher::game
