#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "game/GamePlatform.hpp"
#include "library/RomScanner.hpp"
#include "util/Utf8Path.hpp"

namespace {
namespace fs = std::filesystem;
using launcher::game::GamePlatform;

bool check(bool condition, const std::string &message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

// A byte sequence is enough: the scan decides by name and folder, and the
// import is not exercised here, so no real ROM header is needed.
bool writeFile(const fs::path &path, std::uintmax_t size = 64) {
  std::error_code code;
  fs::create_directories(path.parent_path(), code);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << std::string(static_cast<std::size_t>(size), '\0');
  return output.good();
}

bool names(const std::vector<std::string> &paths, const std::string &name) {
  return std::any_of(paths.begin(), paths.end(),
                     [&name](const std::string &path) { return launcher::paths::fromUtf8(path).filename() == name; });
}

// The scanner hands back the platform's own separators, which are backslashes
// on Windows, so compare the two ends as paths rather than as text.
bool samePath(const std::string &actual, const fs::path &expected) {
  return launcher::paths::fromUtf8(actual).lexically_normal() == expected.lexically_normal();
}

} // namespace

int main() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path root = fs::temp_directory_path() / ("mrg-rom-layout-" + std::to_string(nonce));
  const fs::path roms = root / "Roms";
  using launcher::game::detectPlatform;
  using launcher::game::platformFromDirectory;

  bool ok = true;
  const std::string card = launcher::paths::toUtf8(roms);

  // The folder names the platform, whatever the extension says.
  ok &= check(writeFile(roms / "FC" / "Contra.nes"), "write FC/Contra.nes");
  ok &= check(writeFile(roms / "MD" / "Sonic.bin"), "write MD/Sonic.bin");
  ok &= check(writeFile(roms / "MD" / "Streets of Rage.gen"), "write MD/Streets of Rage.gen");
  ok &= check(writeFile(roms / "SFC" / "Mario.md"), "write SFC/Mario.md");
  ok &= check(writeFile(roms / "GB" / "Tetris.gb"), "write GB/Tetris.gb");
  ok &= check(writeFile(roms / "GBA" / "Fusion.gba"), "write GBA/Fusion.gba");

  // A readme next to the games is not a game, even when its extension is one.
  ok &= check(writeFile(roms / "FC" / "README.md"), "write FC/README.md");
  ok &= check(writeFile(roms / "MD" / "readme.txt"), "write MD/readme.txt");
  ok &= check(writeFile(roms / "GB" / "LICENSE"), "write GB/LICENSE");

  // Outside a system folder the extension still decides.
  ok &= check(writeFile(roms / "Mystery" / "Unknown.nes"), "write Mystery/Unknown.nes");
  ok &= check(writeFile(roms / "Mystery" / "Unknown.md"), "write Mystery/Unknown.md");
  // One level above a system folder is still close enough to name it; a folder
  // further up must not.
  ok &= check(writeFile(roms / "Consoles" / "FC" / "Nested.nes"), "write Consoles/FC/Nested.nes");

  ok &= check(detectPlatform(card + "/FC/Contra.nes") == GamePlatform::Nes, "Roms/FC/Contra.nes is a NES game");
  ok &=
      check(detectPlatform(card + "/MD/Sonic.bin") == GamePlatform::Genesis, "Roms/MD/Sonic.bin is a Mega Drive game");
  ok &= check(detectPlatform(card + "/MD/Streets of Rage.gen") == GamePlatform::Genesis,
              "Roms/MD/Streets of Rage.gen is a Mega Drive game");
  ok &= check(detectPlatform(card + "/SFC/Mario.md") == GamePlatform::Snes,
              "the folder wins over a misleading extension");
  ok &= check(detectPlatform(card + "/GB/Tetris.gb") == GamePlatform::GameBoy, "Roms/GB/Tetris.gb is a Game Boy game");
  ok &= check(detectPlatform(card + "/GBA/Fusion.gba") == GamePlatform::Gba,
              "Roms/GBA/Fusion.gba is a Game Boy Advance game");
  ok &= check(detectPlatform(card + "/FC/README.md") == GamePlatform::Unknown, "Roms/FC/README.md is documentation");
  ok &= check(detectPlatform(card + "/MD/readme.txt") == GamePlatform::Unknown, "Roms/MD/readme.txt is documentation");
  ok &= check(detectPlatform(card + "/GB/LICENSE") == GamePlatform::Unknown, "Roms/GB/LICENSE is a licence");
  ok &= check(detectPlatform(card + "/Mystery/Unknown.nes") == GamePlatform::Nes,
              "an extension outside a system folder still counts");
  ok &= check(detectPlatform(card + "/Mystery/Unknown.md") == GamePlatform::Genesis,
              "a .md outside a system folder is read as a Mega Drive game");
  ok &= check(platformFromDirectory(card + "/Consoles/FC/Nested.nes") == GamePlatform::Nes,
              "one folder above the game still names the system");
  ok &= check(platformFromDirectory(card + "/FC/Sub/Cave/Deeper.nes") == GamePlatform::Unknown,
              "a system folder two levels up is too far");

  // The scan reports what the launcher will show.
  const std::vector<std::string> found = launcher::library::findGames({card});
  ok &= check(names(found, "Contra.nes"), "the scan finds Roms/FC/Contra.nes");
  ok &= check(names(found, "Sonic.bin"), "the scan finds Roms/MD/Sonic.bin");
  ok &= check(names(found, "Mario.md"), "the scan finds Roms/SFC/Mario.md");
  ok &= check(names(found, "Nested.nes"), "the scan finds Roms/Consoles/FC/Nested.nes");
  ok &= check(!names(found, "README.md"), "the scan skips Roms/FC/README.md");
  ok &= check(!names(found, "readme.txt"), "the scan skips Roms/MD/readme.txt");
  ok &= check(!names(found, "LICENSE"), "the scan skips Roms/GB/LICENSE");
  ok &= check(names(found, "Unknown.nes"), "the scan finds a game outside a system folder");

  // A game dropped straight into Roms/ is filed under the folder a card would
  // use for it, so the tree a scan leaves behind reads like a card's.
  using launcher::library::fileLooseRom;
  ok &= check(writeFile(roms / "Loose.nes"), "write Roms/Loose.nes");
  ok &= check(writeFile(roms / "Loose.sfc"), "write Roms/Loose.sfc");
  ok &= check(writeFile(roms / "notes.md"), "write Roms/notes.md");
  ok &= check(writeFile(roms / "README.md"), "write Roms/README.md");
  ok &= check(writeFile(roms / "MyStuff" / "Nested2.nes"), "write Roms/MyStuff/Nested2.nes");
  ok &= check(samePath(fileLooseRom(card + "/Loose.nes"), roms / "FC" / "Loose.nes"),
              "Roms/Loose.nes moves into Roms/FC");
  ok &= check(fs::exists(roms / "FC" / "Loose.nes"), "the filed game is in Roms/FC");
  ok &= check(!fs::exists(roms / "Loose.nes"), "the loose copy is gone");
  ok &= check(samePath(fileLooseRom(card + "/Loose.sfc"), roms / "SFC" / "Loose.sfc"),
              "Roms/Loose.sfc moves into Roms/SFC");
  // `.md` is both a Mega Drive ROM and a Markdown document, so a file with any
  // other name follows its extension, exactly as the import would have read it.
  ok &= check(samePath(fileLooseRom(card + "/notes.md"), roms / "MD" / "notes.md"),
              "an ambiguous .md follows the extension the import uses");
  ok &= check(samePath(fileLooseRom(card + "/README.md"), roms / "README.md"),
              "a readme at the top of Roms/ is documentation and stays put");
  ok &= check(samePath(fileLooseRom(card + "/FC/Contra.nes"), roms / "FC" / "Contra.nes"),
              "a game already in a system folder stays put");
  ok &= check(samePath(fileLooseRom(card + "/MyStuff/Nested2.nes"), roms / "MyStuff" / "Nested2.nes"),
              "only the top of Roms/ is tidied, not a folder of the player's own");
  // A name that already exists in the target is never overwritten: the loose
  // copy is left for a human instead of one ROM quietly replacing another.
  ok &= check(writeFile(roms / "Sonic.gen"), "write Roms/Sonic.gen");
  ok &= check(writeFile(roms / "MD" / "Sonic.gen", 128), "write Roms/MD/Sonic.gen");
  ok &= check(samePath(fileLooseRom(card + "/Sonic.gen"), roms / "Sonic.gen"),
              "an occupied target leaves the loose game where it is");

  std::error_code code;
  fs::remove_all(root, code);
  if (!ok)
    return 1;
  std::cout << "rom-layout: ok\n";
  return 0;
}
