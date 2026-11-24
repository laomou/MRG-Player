#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <SDL.h>

#include "config/AppPaths.hpp"
#include "config/Settings.hpp"
#include "game/GamePlatform.hpp"
#include "library/GameLibrary.hpp"
#include "runtime/RuntimeRegistry.hpp"
#include "util/Utf8Path.hpp"

namespace {
namespace fs = std::filesystem;
using launcher::library::GameLibrary;
using launcher::library::ImportStatus;
using launcher::runtime::RuntimeRegistry;
using launcher::runtime::SaveDataStatus;

bool check(bool condition, const std::string &message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

std::vector<std::uint8_t> makeRom(std::uint8_t fill) {
  std::vector<std::uint8_t> bytes(16 + 16384, fill);
  bytes[0] = 'N';
  bytes[1] = 'E';
  bytes[2] = 'S';
  bytes[3] = 0x1a;
  bytes[4] = 1;
  bytes[5] = 0;
  std::fill(bytes.begin() + 6, bytes.begin() + 16, 0);
  return bytes;
}

bool writeBytes(const fs::path &path, const std::vector<std::uint8_t> &bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  return output.good() && static_cast<bool>(output.write(
                              reinterpret_cast<const char *>(bytes.data()),
                              static_cast<std::streamsize>(bytes.size())));
}

std::vector<std::uint8_t> readBytes(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path root = fs::temp_directory_path() /
                        ("nes-player-library-test-" + std::to_string(nonce));
  const fs::path sources = root / "sources";
  const fs::path managed = root / "managed";
  const fs::path managedNes = managed / "nes";
  std::error_code ec;
  fs::create_directories(sources, ec);
  if (!check(!ec, "create temporary source directory"))
    return 1;

  bool ok = true;
  const auto firstBytes = makeRom(0x11);
  const auto secondBytes = makeRom(0x22);
  const fs::path first = sources / "My Game.nes";
  const fs::path second = sources / "alpha.NES";
  const fs::path invalid = sources / "broken.nes";
  const fs::path wrongExtension = sources / "valid.bin";
  ok &= check(writeBytes(first, firstBytes), "write first test ROM");
  ok &= check(writeBytes(second, secondBytes), "write second test ROM");
  ok &= check(writeBytes(invalid, {1, 2, 3, 4}), "write invalid test ROM");
  ok &= check(writeBytes(wrongExtension, firstBytes),
              "write wrong-extension test ROM");

  const fs::path xdgRoot = root / "xdg";
  SDL_setenv("XDG_DATA_HOME", xdgRoot.string().c_str(), 1);
  const fs::path legacyRoot = launcher::config::legacyDataRoot();
  const fs::path currentRoot = launcher::config::dataRoot();
  fs::create_directories(legacyRoot / "roms", ec);
  fs::create_directories(legacyRoot / "saves", ec);
  ok &= check(!ec, "create isolated legacy data directories");
  ok &= check(writeBytes(legacyRoot / "settings.ini", {0x31}),
              "write legacy settings");
  ok &= check(writeBytes(legacyRoot / "roms" / "legacy.nes", firstBytes),
              "write legacy managed ROM");
  ok &= check(writeBytes(legacyRoot / "saves" / "legacy.sav", {0x5a}),
              "write legacy save");
  std::string migrationError;
  ok &= check(launcher::config::migrateLegacyData(migrationError),
              "migrate legacy data into MRG Player layout");
  ok &= check(readBytes(currentRoot / "settings.ini") ==
                  std::vector<std::uint8_t>{0x31},
              "legacy settings migrate");
  ok &= check(readBytes(currentRoot / "library" / "nes" / "legacy.nes") ==
                  firstBytes,
              "legacy ROMs migrate into platform directory");
  ok &= check(readBytes(currentRoot / "saves" / "nes" / "legacy.sav") ==
                  std::vector<std::uint8_t>{0x5a},
              "legacy saves migrate into platform directory");
  ok &= check(writeBytes(legacyRoot / "settings.ini", {0x32}),
              "change legacy settings after migration");
  ok &= check(launcher::config::migrateLegacyData(migrationError),
              "repeat migration safely");
  ok &= check(readBytes(currentRoot / "settings.ini") ==
                  std::vector<std::uint8_t>{0x31},
              "migration never overwrites current settings");

  GameLibrary library(managed.string());
  ok &= check(library.initialize(), "initialize managed library");
  ok &= check(library.entries().empty(), "new library starts empty");
  ok &= check(library.slotCount() == 7, "empty library retains seven slots");

  ok &= check(library.importGame(first.string()) == ImportStatus::Imported,
              "import valid ROM");
  ok &= check(library.entries().size() == 1, "one imported entry is visible");
  ok &= check(library.entries()[0].title == "My Game",
              "hash suffix is hidden from title");
  const fs::path managedFirst = library.entries()[0].filePath;
  ok &= check(managedFirst.parent_path() == managedNes,
              "ROM is copied into managed directory");
  ok &= check(managedFirst != first, "managed entry does not use source path");
  ok &= check(readBytes(managedFirst) == firstBytes,
              "managed ROM bytes match source");
  ok &= check(library.lastImportedIndex() == 0,
              "newly imported entry is selected");

  ok &=
      check(library.importGame(first.string()) == ImportStatus::AlreadyPresent,
            "duplicate content is detected");
  ok &= check(library.entries().size() == 1,
              "duplicate import does not create another copy");
  ok &= check(library.importGame(invalid.string()) == ImportStatus::Failed,
              "invalid iNES file is rejected");
  ok &= check(library.error() == "ROM is smaller than the iNES header",
              "invalid iNES error is precise");
  ok &=
      check(library.importGame(wrongExtension.string()) == ImportStatus::Failed,
            "wrong extension is rejected");
  ok &=
      check(library.error() == "No installed runtime supports this game format",
            "unsupported format reports missing runtime");

  ok &= check(writeBytes(managedNes / "manual-broken.nes", {0, 1, 2, 3}),
              "write damaged managed ROM");
  GameLibrary rescanned(managed.string());
  ok &= check(rescanned.initialize(), "rescan managed library");
  ok &= check(rescanned.entries().size() == 1,
              "startup scan ignores damaged ROMs");
  ok &= check(rescanned.entries()[0].filePath == managedFirst.string(),
              "startup scan restores managed entry");

  ok &= check(rescanned.importGame(second.string()) == ImportStatus::Imported,
              "uppercase .NES extension is accepted");
  ok &= check(rescanned.entries().size() == 2, "second unique ROM is imported");
  ok &= check(rescanned.entries()[0].title == "alpha" &&
                  rescanned.entries()[1].title == "My Game",
              "library entries sort case-insensitively");
  ok &= check(rescanned.slotCount() == 7,
              "small populated library retains seven carousel slots");

  const std::string firstId = library.entries()[0].id;
  const int metadataIndex =
      rescanned.indexOfId(firstId, launcher::game::GamePlatform::Nes);
  ok &= check(metadataIndex >= 0, "find game by stable platform identity");
  ok &= check(metadataIndex >= 0 &&
                  rescanned.setCustomTitle(
                      static_cast<std::size_t>(metadataIndex), "Custom % Name"),
              "persist custom game title");
  const int titledIndex =
      rescanned.indexOfId(firstId, launcher::game::GamePlatform::Nes);
  ok &= check(
      titledIndex >= 0 &&
          rescanned.setFavorite(static_cast<std::size_t>(titledIndex), true),
      "persist favorite game status");
  ok &= check(rescanned.recordPlaySession(
                  firstId, launcher::game::GamePlatform::Nes, 42, 100),
              "record first play session");
  ok &= check(rescanned.recordPlaySession(
                  firstId, launcher::game::GamePlatform::Nes, 8, 90),
              "accumulate play time without regressing last-played timestamp");
  const int updatedIndex =
      rescanned.indexOfId(firstId, launcher::game::GamePlatform::Nes);
  ok &= check(updatedIndex == 0, "favorite game sorts before regular games");
  ok &= check(updatedIndex >= 0 && rescanned.entries()[updatedIndex].favorite,
              "favorite flag is visible in library entry");
  ok &= check(updatedIndex >= 0 &&
                  rescanned.entries()[updatedIndex].title == "Custom % Name",
              "custom title is visible in library entry");
  ok &= check(updatedIndex >= 0 &&
                  rescanned.entries()[updatedIndex].playSeconds == 50 &&
                  rescanned.entries()[updatedIndex].lastPlayed == 100,
              "play metadata accumulates and keeps latest timestamp");
  GameLibrary metadataReload(managed.string());
  ok &= check(metadataReload.initialize(), "reload library metadata from disk");
  const int reloadedIndex =
      metadataReload.indexOfId(firstId, launcher::game::GamePlatform::Nes);
  ok &= check(
      reloadedIndex >= 0 && metadataReload.entries()[reloadedIndex].favorite &&
          metadataReload.entries()[reloadedIndex].title == "Custom % Name" &&
          metadataReload.entries()[reloadedIndex].playSeconds == 50 &&
          metadataReload.entries()[reloadedIndex].lastPlayed == 100,
      "favorite, title and play metadata survive restart");

  const fs::path saves = root / "saves";
  fs::create_directories(saves, ec);
  ok &= check(!ec, "create test save directory");
  std::string saveError;
  RuntimeRegistry registry;
  const auto firstEntry = library.entries()[0];
  ok &= check(registry.supports(launcher::game::GamePlatform::Nes),
              "registry routes NES through a libretro core");
  ok &= check(!registry.supports(launcher::game::GamePlatform::Unknown),
              "registry rejects unknown platforms");
  ok &= check(firstEntry.platform == launcher::game::GamePlatform::Nes,
              "import records detected platform");
  ok &= check(!firstEntry.id.empty(), "import records stable game identity");
  ok &=
      check(launcher::game::platformDescriptor(firstEntry.platform).id == "nes",
            "platform descriptor exposes stable NES id");
  ok &= check(registry.inspectSaveData(firstEntry, saveError, saves.string()) ==
                  SaveDataStatus::Missing,
              "save inspection reports missing data");
  const fs::path savePath = saves / (firstEntry.id + ".srm");
  ok &= check(writeBytes(savePath, {0x5a, 0xa5}), "write matching save data");
  ok &= check(registry.inspectSaveData(firstEntry, saveError, saves.string()) ==
                  SaveDataStatus::Present,
              "save inspection finds cartridge save data");
  ok &= check(registry.deleteSaveData(firstEntry, saveError, saves.string()) ==
                  SaveDataStatus::Removed,
              "delete save data removes only the save file");
  ok &= check(fs::exists(managedFirst), "deleting save data keeps managed ROM");
  ok &= check(registry.deleteSaveData(firstEntry, saveError, saves.string()) ==
                  SaveDataStatus::Missing,
              "deleting absent save data is harmless");

  ok &= check(writeBytes(savePath, {0x33}), "restore save before ROM removal");
  const int firstIndex = rescanned.indexOfPath(managedFirst.string());
  ok &= check(firstIndex >= 0, "find imported ROM before removal");
  ok &= check(firstIndex >= 0 &&
                  rescanned.removeGame(static_cast<std::size_t>(firstIndex)),
              "remove selected ROM from managed library");
  ok &= check(!fs::exists(managedFirst), "managed ROM file is deleted");
  ok &= check(fs::exists(savePath), "removing ROM retains separate save data");
  ok &= check(rescanned.entries().size() == 1,
              "library refreshes after ROM removal");
  ok &= check(!rescanned.removeGame(99), "invalid removal index is rejected");
  ok &=
      check(rescanned.error() == "Selected game is not in the managed library",
            "invalid removal error is precise");

  ok &= check(rescanned.importGame(first.string()) == ImportStatus::Imported,
              "reimport removed game");
  const int restoredMetadataIndex =
      rescanned.indexOfId(firstId, launcher::game::GamePlatform::Nes);
  ok &= check(restoredMetadataIndex >= 0 &&
                  rescanned.entries()[restoredMetadataIndex].favorite &&
                  rescanned.entries()[restoredMetadataIndex].title ==
                      "Custom % Name" &&
                  rescanned.entries()[restoredMetadataIndex].playSeconds == 50,
              "metadata survives ROM removal and reimport");

  // A game can carry its own display options, stored next to the rest of the
  // metadata so they follow the library directory.
  launcher::runtime::VideoOptions overrideVideo;
  overrideVideo.aspect = launcher::runtime::AspectMode::Pixels8x7;
  overrideVideo.integerScale = true;
  overrideVideo.overscan = 8;
  ok &= check(rescanned.setVideoOverride(firstId,
                                         launcher::game::GamePlatform::Nes,
                                         overrideVideo),
              "persist per-game display override");
  launcher::runtime::VideoOptions overrideReadBack;
  ok &= check(rescanned.videoOverride(firstId,
                                      launcher::game::GamePlatform::Nes,
                                      overrideReadBack) &&
                  overrideReadBack.aspect ==
                      launcher::runtime::AspectMode::Pixels8x7 &&
                  overrideReadBack.integerScale &&
                  overrideReadBack.overscan == 8,
              "display override reads back");
  ok &= check(!rescanned.videoOverride("missing-id",
                                      launcher::game::GamePlatform::Nes,
                                      overrideReadBack),
              "games without an override report none");
  GameLibrary overrideReload(managed.string());
  ok &= check(overrideReload.initialize() &&
                  overrideReload.videoOverride(
                      firstId, launcher::game::GamePlatform::Nes,
                      overrideReadBack) &&
                  overrideReadBack.overscan == 8,
              "display override survives restart");
  ok &= check(rescanned.clearVideoOverride(firstId,
                                           launcher::game::GamePlatform::Nes),
              "clear per-game display override");
  ok &= check(!rescanned.videoOverride(firstId,
                                      launcher::game::GamePlatform::Nes,
                                      overrideReadBack),
              "cleared override is gone");

  // V1 metadata still loads, and the next save upgrades it to V2.
  const fs::path legacyMetadataDir = root / "legacy-metadata";
  fs::create_directories(legacyMetadataDir, ec);
  {
    std::ofstream output(legacyMetadataDir / "metadata.tsv", std::ios::trunc);
    output << "MRG_LIBRARY_METADATA_V1" << "\n" << "1\t" << firstId
           << "\t1\t77\t1234\tLegacy Title" << "\n";
  }
  GameLibrary legacyMetadata(legacyMetadataDir.string());
  ok &= check(legacyMetadata.initialize(), "load V1 library metadata");
  launcher::runtime::VideoOptions legacyProbe;
  legacyProbe.overscan = 4;
  ok &= check(!legacyMetadata.videoOverride(firstId,
                                           launcher::game::GamePlatform::Nes,
                                           legacyProbe) &&
                  legacyProbe.overscan == 4,
              "V1 metadata has no display override");
  ok &= check(legacyMetadata.setVideoOverride(firstId,
                                              launcher::game::GamePlatform::Nes,
                                              overrideVideo),
              "saving upgrades V1 metadata");
  {
    std::ifstream upgraded(legacyMetadataDir / "metadata.tsv");
    std::string upgradedHeader;
    std::getline(upgraded, upgradedHeader);
    const std::string upgradedRow{std::istreambuf_iterator<char>(upgraded),
                                  std::istreambuf_iterator<char>()};
    ok &= check(upgradedHeader == "MRG_LIBRARY_METADATA_V2",
                "metadata is rewritten as V2");
    ok &= check(upgradedRow.find("Legacy Title") != std::string::npos,
                "upgrade keeps the V1 title");
  }

  launcher::AppSettings writtenSettings;
  writtenSettings.language = 1;
  writtenSettings.volume = 37;
  writtenSettings.selectedGame = 12;
  writtenSettings.promptDevice = launcher::config::PromptDeviceMode::Controller;
  writtenSettings.controllerStyle = launcher::config::ControllerStyle::Nintendo;
  writtenSettings.preferredControllerGuid = "03000000deadbeef03000000deadbeef";
  writtenSettings.input
      .keyboard[static_cast<std::size_t>(launcher::config::InputAction::A)] =
      SDLK_q;
  writtenSettings.input
      .controller[static_cast<std::size_t>(launcher::config::InputAction::A)] =
      SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
  writtenSettings.input.analogDeadZone = 23456;
  launcher::saveSettings(writtenSettings);
  launcher::AppSettings loadedSettings;
  launcher::loadSettings(loadedSettings);
  ok &= check(loadedSettings.language == 1 && loadedSettings.volume == 37 &&
                  loadedSettings.selectedGame == 12,
              "general settings persist");
  ok &=
      check(loadedSettings.promptDevice ==
                    launcher::config::PromptDeviceMode::Controller &&
                loadedSettings.controllerStyle ==
                    launcher::config::ControllerStyle::Nintendo &&
                loadedSettings.preferredControllerGuid ==
                    "03000000deadbeef03000000deadbeef",
            "prompt device, controller style and preferred controller persist");
  ok &= check(loadedSettings.input.keyboard[static_cast<std::size_t>(
                  launcher::config::InputAction::A)] == SDLK_q &&
                  loadedSettings.input.controller[static_cast<std::size_t>(
                      launcher::config::InputAction::A)] ==
                      SDL_CONTROLLER_BUTTON_RIGHTSHOULDER &&
                  loadedSettings.input.analogDeadZone == 23456,
              "keyboard, controller and dead-zone bindings persist");
  ok &= check(launcher::config::resolvePromptDevice(
                  launcher::config::PromptDeviceMode::Auto,
                  launcher::config::InputDeviceKind::Controller,
                  false) == launcher::config::InputDeviceKind::KeyboardMouse,
              "automatic prompts fall back when no controller is connected");
  ok &= check(launcher::config::resolvePromptDevice(
                  launcher::config::PromptDeviceMode::Controller,
                  launcher::config::InputDeviceKind::KeyboardMouse,
                  false) == launcher::config::InputDeviceKind::Controller,
              "manual controller prompt mode remains selected");
  ok &=
      check(launcher::config::controllerButtonPrompt(
                SDL_CONTROLLER_BUTTON_A,
                launcher::config::ControllerStyle::Nintendo) == "B" &&
                launcher::config::controllerButtonPrompt(
                    SDL_CONTROLLER_BUTTON_X,
                    launcher::config::ControllerStyle::PlayStation) == "SQUARE",
            "controller prompts follow Nintendo and PlayStation layouts");
  const launcher::config::InputPrompts keyboardPrompts =
      launcher::config::inputPrompts(
          launcher::config::InputDeviceKind::KeyboardMouse,
          launcher::config::ControllerStyle::Generic);
  ok &=
      check(keyboardPrompts.confirm == "ENTER" &&
                keyboardPrompts.manage == "X" && keyboardPrompts.back == "ESC",
            "keyboard prompt set matches launcher controls");

  // A ROM whose name is not ASCII has to survive import, rescan and metadata
  // reload. Windows would otherwise read these bytes in the ANSI code page.
  const fs::path unicodeManaged = root / "unicode-managed";
  const fs::path unicodeRom =
      sources / launcher::paths::fromUtf8("超级马里奥.nes");
  ok &= check(writeBytes(unicodeRom, makeRom(0x33)),
              "write a ROM whose name is not ASCII");
  {
    GameLibrary unicodeLibrary(unicodeManaged.string());
    ok &= check(unicodeLibrary.initialize(),
                "initialize the library for non-ASCII names");
    ok &= check(unicodeLibrary.importGame(
                    launcher::paths::toUtf8(unicodeRom)) ==
                    ImportStatus::Imported,
                "import a ROM whose name is not ASCII");
    const auto unicodeEntry = std::find_if(
        unicodeLibrary.entries().begin(), unicodeLibrary.entries().end(),
        [](const launcher::library::GameEntry &entry) {
          return entry.title == "超级马里奥";
        });
    ok &= check(unicodeEntry != unicodeLibrary.entries().end(),
                "the non-ASCII title is kept in the library");
    if (unicodeEntry != unicodeLibrary.entries().end()) {
      const fs::path managed =
          launcher::paths::fromUtf8(unicodeEntry->filePath);
      ok &= check(readBytes(managed) == makeRom(0x33),
                  "the managed copy of a non-ASCII ROM is readable");
    }
  }
  {
    GameLibrary unicodeReload(unicodeManaged.string());
    ok &= check(unicodeReload.initialize(),
                "reload the library holding non-ASCII names");
    const auto reloaded = std::find_if(
        unicodeReload.entries().begin(), unicodeReload.entries().end(),
        [](const launcher::library::GameEntry &entry) {
          return entry.title == "超级马里奥";
        });
    ok &= check(reloaded != unicodeReload.entries().end(),
                "the non-ASCII entry survives a restart");
  }

  fs::remove_all(root, ec);
  ok &= check(!ec, "remove temporary test directory");
  if (!ok)
    return 1;
  std::cout << "GameLibrary tests passed\n";
  return 0;
}
