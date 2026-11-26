#include "game/GamePlatform.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "util/Utf8Path.hpp"

namespace launcher::game {
namespace {
namespace fs = std::filesystem;

constexpr std::uintmax_t kMaximumGameSize = 64U * 1024U * 1024U;

constexpr PlatformDescriptor kUnknown{GamePlatform::Unknown, "unknown", "UNKNOWN", "GAME FILE", "unknown"};

constexpr std::array<PlatformDescriptor, 5> kDescriptors{{
    {GamePlatform::Nes, "nes", "NES", ".NES ROM", "nes"},
    {GamePlatform::Gba, "gba", "GAME BOY ADVANCE", ".GBA ROM", "gba"},
    {GamePlatform::Snes, "snes", "SUPER NES", ".SFC ROM", "snes"},
    {GamePlatform::GameBoy, "gb", "GAME BOY", ".GB ROM", "gb"},
    {GamePlatform::Genesis, "genesis", "MEGA DRIVE", ".MD ROM", "genesis"},
}};

struct ExtensionMap {
  std::string_view extension;
  GamePlatform platform;
};

constexpr std::array<ExtensionMap, 9> kExtensions{{
    {".nes", GamePlatform::Nes},
    {".gba", GamePlatform::Gba},
    {".sfc", GamePlatform::Snes},
    {".smc", GamePlatform::Snes},
    // A card is filled from many sources, and .snes is as common as .sfc.
    {".snes", GamePlatform::Snes},
    {".gb", GamePlatform::GameBoy},
    {".gbc", GamePlatform::GameBoy},
    {".md", GamePlatform::Genesis},
    {".gen", GamePlatform::Genesis},
}};

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

} // namespace

const PlatformDescriptor &platformDescriptor(GamePlatform platform) noexcept {
  for (const PlatformDescriptor &descriptor : kDescriptors) {
    if (descriptor.platform == platform)
      return descriptor;
  }
  return kUnknown;
}

GamePlatform platformFromExtension(const std::string &path) noexcept {
  const std::string extension =
      lowerAscii(paths::toUtf8(paths::fromUtf8(path).extension()));
  for (const ExtensionMap &entry : kExtensions) {
    if (entry.extension == extension)
      return entry.platform;
  }
  return GamePlatform::Unknown;
}

std::vector<std::string> gameExtensions() {
  std::vector<std::string> extensions;
  extensions.reserve(kExtensions.size());
  for (const ExtensionMap &entry : kExtensions)
    extensions.emplace_back(entry.extension.substr(1));
  return extensions;
}

bool validateGameFile(GamePlatform platform, const std::string &path, std::string &error) {
  error.clear();
  if (platformDescriptor(platform).platform == GamePlatform::Unknown) {
    error = "No installed runtime supports this game format";
    return false;
  }

  const fs::path gamePath = paths::fromUtf8(path);
  std::error_code ec;
  const std::uintmax_t size = fs::file_size(gamePath, ec);
  if (ec) {
    error = "Cannot read game file size: " + ec.message();
    return false;
  }
  if (size > kMaximumGameSize) {
    error = "Game file exceeds the 64 MiB import limit";
    return false;
  }

  // Only the NES header carries a magic number the importer can check. The
  // libretro cores validate their own formats when the game is loaded.
  if (platform != GamePlatform::Nes)
    return true;

  if (size < 16) {
    error = "ROM is smaller than the iNES header";
    return false;
  }

  std::array<std::uint8_t, 4> magic{};
  std::ifstream input(gamePath, std::ios::binary);
  if (!input || !input.read(reinterpret_cast<char *>(magic.data()), static_cast<std::streamsize>(magic.size()))) {
    error = "Cannot read game file";
    return false;
  }
  if (magic != std::array<std::uint8_t, 4>{'N', 'E', 'S', 0x1a}) {
    error = "File does not contain an iNES header";
    return false;
  }
  return true;
}

} // namespace launcher::game
