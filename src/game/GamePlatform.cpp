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

constexpr PlatformDescriptor kUnknown{GamePlatform::Unknown, "unknown", "UNKNOWN", "GAME FILE", "unknown",
                                    "", ""};

constexpr std::array<PlatformDescriptor, 5> kDescriptors{{
    {GamePlatform::Nes, "nes", "FC (NES)", ".NES ROM", "FC", "FC", "nes"},
    {GamePlatform::Gba, "gba", "GAME BOY ADVANCE", ".GBA ROM", "GBA", "GBA", "gba"},
    {GamePlatform::Snes, "snes", "SFC (SNES)", ".SFC ROM", "SFC", "SFC", "snes"},
    {GamePlatform::GameBoy, "gb", "GAME BOY", ".GB ROM", "GB", "GB", "gb"},
    {GamePlatform::Genesis, "genesis", "MD (GENESIS)", ".MD ROM", "MD", "MD", "genesis"},
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

struct DirectoryMap {
  std::string_view directory;
  GamePlatform platform;
};

// The system folders the stock launcher writes on a card. Its own list also has
// ARCADE, NEOGEO, PCE, PS and MS; no runtime here serves those yet, so a file in
// one of them stays Unknown instead of being handed to a wrong core.
constexpr std::array<DirectoryMap, 10> kDirectories{{
    {"fc", GamePlatform::Nes},
    {"nes", GamePlatform::Nes},
    {"sfc", GamePlatform::Snes},
    {"snes", GamePlatform::Snes},
    {"md", GamePlatform::Genesis},
    {"genesis", GamePlatform::Genesis},
    {"megadrive", GamePlatform::Genesis},
    {"gb", GamePlatform::GameBoy},
    {"gbc", GamePlatform::GameBoy},
    {"gba", GamePlatform::Gba},
}};

// A ROM set ships its readme and licence next to the games, and .md is both a
// Mega Drive extension and the extension of a readme.
constexpr std::array<std::string_view, 7> kDocumentationStems{{
    "readme", "license", "licence", "copying", "changelog", "changes", "credits",
}};

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool isDocumentation(const fs::path &path) {
  std::string stem = lowerAscii(paths::toUtf8(path.filename()));
  const std::size_t dot = stem.find('.');
  if (dot != std::string::npos)
    stem.resize(dot);
  return std::find(kDocumentationStems.begin(), kDocumentationStems.end(), stem) !=
         kDocumentationStems.end();
}

// The system a folder name names. Emulator packs install as "FC (FCEUmm)"
// folders, so a written-out qualifier is accepted as well. Nothing shorter is:
// an unrelated folder that merely starts with the letters of a system, such as
// a scratch directory called "nes-old", must not decide anything.
GamePlatform platformForDirectoryName(std::string_view name) {
  for (const DirectoryMap &entry : kDirectories) {
    if (name == entry.directory)
      return entry.platform;
    if (name.size() > entry.directory.size() &&
        name.compare(0, entry.directory.size(), entry.directory) == 0) {
      const char next = name[entry.directory.size()];
      if (next == ' ' || next == '(')
        return entry.platform;
    }
  }
  return GamePlatform::Unknown;
}

} // namespace

const PlatformDescriptor &platformDescriptor(GamePlatform platform) noexcept {
  for (const PlatformDescriptor &descriptor : kDescriptors) {
    if (descriptor.platform == platform)
      return descriptor;
  }
  return kUnknown;
}

std::vector<PlatformDescriptor> platformDescriptors() {
  return {kDescriptors.begin(), kDescriptors.end()};
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

GamePlatform platformFromDirectory(const std::string &path) noexcept {
  fs::path parent = paths::fromUtf8(path).parent_path();
  // Roms/FC/<game> is the stock layout; the second level covers a card that
  // groups the systems once more (Roms/Consoles/FC/<game>) without letting a
  // folder further up decide what a game is.
  for (int level = 0; level < 2 && !parent.empty(); ++level) {
    const std::string name = lowerAscii(paths::toUtf8(parent.filename()));
    if (!name.empty()) {
      const GamePlatform platform = platformForDirectoryName(name);
      if (platform != GamePlatform::Unknown)
        return platform;
    }
    parent = parent.parent_path();
  }
  return GamePlatform::Unknown;
}

GamePlatform detectPlatform(const std::string &path) noexcept {
  // A readme beside the games is documentation, not a ROM, even when its
  // extension is a game one.
  if (isDocumentation(paths::fromUtf8(path)))
    return GamePlatform::Unknown;
  const GamePlatform fromDirectory = platformFromDirectory(path);
  if (fromDirectory != GamePlatform::Unknown)
    return fromDirectory;
  return platformFromExtension(path);
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
