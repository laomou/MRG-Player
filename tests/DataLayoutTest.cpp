#include <SDL.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "config/AppPaths.hpp"
#include "config/DataLayout.hpp"
#include "game/GamePlatform.hpp"

namespace fs = std::filesystem;

namespace {

// A path list is separated the way the host names one, and on Windows the colon
// belongs to the drive letter.
#if defined(_WIN32)
constexpr char kPathListSeparator = ';';
#else
constexpr char kPathListSeparator = ':';
#endif

int failures = 0;

bool check(bool condition, const std::string &what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
  }
  return condition;
}

void setEnvironment(const char *name, const std::string &value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

void writeFile(const fs::path &path, const std::string &text) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream stream(path, std::ios::binary);
  stream << text;
}

std::string readFile(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool samePath(const fs::path &left, const fs::path &right) {
  return left.lexically_normal() == right.lexically_normal();
}

} // namespace

int main() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path root = fs::temp_directory_path() / ("mrg-data-layout-" + std::to_string(nonce));
  const fs::path data = root / "data";
  const fs::path card = root / "card";
  std::error_code ec;
  fs::create_directories(data, ec);
  fs::create_directories(card, ec);
  if (!check(!ec, "create the scratch data root"))
    return 1;

  setEnvironment("MRG_DATA_DIR", data.string());
  setEnvironment("MRG_CARD_ROOT", card.string());

  // Every system folder carries the card's name, and the name it used before is
  // remembered so the data below can be moved.
  for (const launcher::game::PlatformDescriptor &descriptor :
       launcher::game::platformDescriptors()) {
    check(descriptor.managedDirectory == descriptor.cardDirectory,
          "the managed folder does not use the card's name");
    check(!descriptor.legacyManagedDirectory.empty(),
          "a platform has no previous folder name to migrate from");
  }

  // A library, save and instant state written by the previous build are found
  // under the old names.
  writeFile(data / "library" / "metadata.tsv", "MRG_LIBRARY_METADATA_V2\n");
  writeFile(data / "library" / "nes" / "rom__abc.nes", "rom");
  writeFile(data / "saves" / "nes" / "game.srm", "save");
  writeFile(data / "states" / "nes" / "game-slot1.mrgstate", "state");
  writeFile(data / "cores-unpacked" / "fceumm_libretro.so", "core");
  writeFile(data / "cover-names.json", "{\"old\":\"name\"}");
  writeFile(data / "rom-cache" / "staged.nes", "staged");

  setEnvironment("MRG_ROM_DIRS", (card / "Roms").string());

  launcher::config::migrateDataLayout();

  check(fs::is_regular_file(data / "library" / "FC" / "rom__abc.nes"),
        "the managed ROM did not move to the card's folder name");
  check(!fs::exists(data / "library" / "nes"), "the old library folder was left behind");
  check(fs::is_regular_file(data / "saves" / "FC" / "game.srm"), "the battery save did not move");
  check(!fs::exists(data / "saves" / "nes"), "the old save folder was left behind");
  check(fs::is_regular_file(data / "states" / "FC" / "game-slot1.mrgstate"),
        "the instant state did not move");
  check(fs::is_regular_file(data / "library" / "metadata.tsv"),
        "the migration disturbed the library metadata");
  check(fs::is_regular_file(data / "cores" / "unpacked" / "fceumm_libretro.so"),
        "the unpacked core did not move inside cores/");
  check(!fs::exists(data / "cores-unpacked"), "the old core folder was left behind");
  check(fs::is_regular_file(data / "library" / "cover-names.json"),
        "the cover name map did not move next to the library");
  check(!fs::exists(data / "cover-names.json"), "the old cover name map was left behind");

  // Running again must be harmless: the folders are gone and the files are
  // already in place.
  launcher::config::migrateDataLayout();
  check(readFile(data / "saves" / "FC" / "game.srm") == "save",
        "a second migration damaged the save");

  // A file the newer build already wrote wins, and the folder it came from is
  // kept as long as it still holds something: nothing is ever overwritten.
  writeFile(data / "saves" / "MD" / "keep.srm", "current");
  writeFile(data / "saves" / "genesis" / "keep.srm", "legacy");
  writeFile(data / "saves" / "genesis" / "move.srm", "legacy-only");
  launcher::config::migrateDataLayout();
  check(readFile(data / "saves" / "MD" / "keep.srm") == "current",
        "the migration overwrote a newer save");
  check(readFile(data / "saves" / "MD" / "move.srm") == "legacy-only",
        "an older save next to a newer one did not move");
  check(fs::exists(data / "saves" / "genesis" / "keep.srm"),
        "the migration deleted a save it could not move");

  // A cover name map that is already in its new home is not replaced by an old
  // copy left in the root.
  writeFile(data / "cover-names.json", "{\"stale\":\"name\"}");
  launcher::config::migrateDataLayout();
  check(readFile(data / "library" / "cover-names.json") == "{\"old\":\"name\"}",
        "an old cover name map replaced the current one");
  check(fs::is_regular_file(data / "cover-names.json"),
        "an old cover name map was deleted instead of being left alone");

  // A staged ROM copy a crash left behind is dropped, and the cache folder with
  // it; a run stages the copy it needs again.
  launcher::config::clearRomCache();
  check(!fs::exists(data / "rom-cache"), "the staged ROM copy survived the clean-up");

  // Box art lands on the card, not inside the pak: the card's Imgs/ comes first
  // and the pak's own folder is never a write target.
  const fs::path otherRoms = root / "extra" / "Roms";
  const std::string romList = (card / "Roms").string() + kPathListSeparator +
                              otherRoms.string() + kPathListSeparator +
                              launcher::config::executableDirectory() + "roms";
  setEnvironment("MRG_ROM_DIRS", romList);
  const std::vector<std::string> covers = launcher::config::coverDirectories();
  check(!covers.empty() && samePath(fs::path(covers.front()), card / "Imgs"),
        "the card's Imgs/ is not the first place a cover is written");
  bool sawBesideRomTree = false;
  bool sawInsidePak = false;
  const fs::path pakImgs = fs::path(launcher::config::executableDirectory()) / "Imgs";
  for (const std::string &entry : covers) {
    if (samePath(fs::path(entry), root / "extra" / "Imgs"))
      sawBesideRomTree = true;
    if (samePath(fs::path(entry), pakImgs))
      sawInsidePak = true;
  }
  check(sawBesideRomTree, "an Imgs/ folder beside an added ROM tree is not searched");
  check(!sawInsidePak, "a cover may be written inside the pak");

  // A desktop has no card, so the data root stands in for one and the cover goes
  // beside the Roms/ tree that sits there.
  setEnvironment("MRG_ROM_DIRS", "");
  setEnvironment("MRG_CARD_ROOT", "");
  const std::vector<std::string> desktopCovers = launcher::config::coverDirectories();
  check(!desktopCovers.empty() && samePath(fs::path(desktopCovers.front()), data / "Imgs"),
        "a desktop does not write a cover beside its Roms/ tree");

  fs::remove_all(root, ec);
  if (failures == 0)
    std::cout << "data-layout: ok\n";
  else
    std::cerr << failures << " data layout checks failed\n";
  return failures == 0 ? 0 : 1;
}
