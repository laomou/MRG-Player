// Exercises the libretro runtime end to end against a stub core: discovery,
// loading, frame stepping, save states with thumbnails, battery saves and the
// error path when no core is installed.

#include <SDL.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "config/AppPaths.hpp"
#include "game/GamePlatform.hpp"
#include "library/GameLibrary.hpp"
#include "runtime/IGameRuntime.hpp"
#include "runtime/RuntimeRegistry.hpp"
#include "runtime/libretro/CoreDiscovery.hpp"
#include "runtime/libretro/CoreInstaller.hpp"
#include "util/ImageFile.hpp"
#include "util/Utf8Path.hpp"

#if defined(MRG_HAVE_ZLIB)
#include <zlib.h>
#endif

namespace {
namespace fs = std::filesystem;

// setenv is POSIX; the test suite also has to compile where the launcher ships.
void setTestEnvironment(const char *name, const char *value, int = 1) {
#if defined(_WIN32)
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

void clearTestEnvironment(const char *name) {
#if defined(_WIN32)
  _putenv_s(name, "");
#else
  unsetenv(name);
#endif
}

// mkdtemp is POSIX only; the suite also has to run on Windows, so build a
// unique directory under the system temp directory instead.
fs::path makeTemporaryDirectory(const char *prefix) {
  const fs::path base = fs::temp_directory_path();
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  for (int attempt = 0; attempt < 100; ++attempt) {
    const fs::path candidate =
        base / (std::string(prefix) + std::to_string(stamp) + "-" +
                std::to_string(attempt));
    std::error_code error;
    if (fs::create_directories(candidate, error) && !error)
      return candidate;
  }
  return {};
}

int failures = 0;

void expect(bool condition, const std::string &message) {
  if (condition)
    return;
  std::fprintf(stderr, "FAIL: %s\n", message.c_str());
  ++failures;
}

bool readAll(const std::string &path, std::vector<std::uint8_t> &bytes) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    return false;
  const std::streamoff size = input.tellg();
  if (size <= 0)
    return false;
  bytes.resize(static_cast<std::size_t>(size));
  input.seekg(0);
  return static_cast<bool>(input.read(reinterpret_cast<char *>(bytes.data()), size));
}

// std::filesystem::path::c_str() is wide on Windows, so paths go through
// string() before they reach the narrow environment helper.
void setTestEnvironment(const char *name, const fs::path &value, int overwrite) {
  setTestEnvironment(name, value.string().c_str(), overwrite);
}

// Points the core search at an empty directory and returns the old value.
std::string rotateDirectory(const char *variable) {
  const char *old = std::getenv(variable);
  const std::string previous = old != nullptr ? old : "";
  const char *base = SDL_GetBasePath();
  const fs::path empty = fs::path(base != nullptr ? base : ".") / "no-cores-here";
  std::error_code ignored;
  fs::create_directories(empty, ignored);
  setTestEnvironment(variable, empty, 1);
  return previous;
}

void restoreDirectory(const char *variable, const std::string &previous) {
  if (previous.empty())
    clearTestEnvironment(variable);
  else
    setTestEnvironment(variable, previous.c_str(), 1);
}

#if defined(MRG_HAVE_ZLIB)
struct ZipEntry {
  std::string name;
  std::vector<std::uint8_t> data;
  bool deflate = false;
};

void put16(std::vector<std::uint8_t> &out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
}

void put32(std::vector<std::uint8_t> &out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8)
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
}

bool deflateRaw(const std::vector<std::uint8_t> &input, std::vector<std::uint8_t> &output) {
  z_stream stream{};
  if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    return false;
  output.resize(deflateBound(&stream, static_cast<uLong>(input.size())));
  stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out = reinterpret_cast<Bytef *>(output.data());
  stream.avail_out = static_cast<uInt>(output.size());
  const bool finished = deflate(&stream, Z_FINISH) == Z_STREAM_END;
  output.resize(stream.total_out);
  deflateEnd(&stream);
  return finished;
}

// Mirrors the layout the libretro buildbot publishes, so discovery sees the same
// bytes a player would download.
bool writeZip(const fs::path &path, const std::vector<ZipEntry> &entries) {
  std::vector<std::uint8_t> archive;
  std::vector<std::vector<std::uint8_t>> payloads;
  for (const ZipEntry &entry : entries) {
    std::vector<std::uint8_t> payload = entry.data;
    if (entry.deflate && !deflateRaw(entry.data, payload))
      return false;
    payloads.push_back(payload);
  }
  std::vector<std::uint32_t> offsets;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const ZipEntry &entry = entries[index];
    const std::vector<std::uint8_t> &payload = payloads[index];
    const std::uint32_t crc = crc32(0, entry.data.data(), static_cast<uInt>(entry.data.size()));
    offsets.push_back(static_cast<std::uint32_t>(archive.size()));
    put32(archive, 0x04034b50U);
    put16(archive, 20);
    put16(archive, 0);
    put16(archive, entry.deflate ? 8 : 0);
    put16(archive, 0);
    put16(archive, 0);
    put32(archive, crc);
    put32(archive, static_cast<std::uint32_t>(payload.size()));
    put32(archive, static_cast<std::uint32_t>(entry.data.size()));
    put16(archive, static_cast<std::uint16_t>(entry.name.size()));
    put16(archive, 0);
    archive.insert(archive.end(), entry.name.begin(), entry.name.end());
    archive.insert(archive.end(), payload.begin(), payload.end());
  }
  const std::uint32_t directoryOffset = static_cast<std::uint32_t>(archive.size());
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const ZipEntry &entry = entries[index];
    const std::vector<std::uint8_t> &payload = payloads[index];
    const std::uint32_t crc = crc32(0, entry.data.data(), static_cast<uInt>(entry.data.size()));
    put32(archive, 0x02014b50U);
    put16(archive, 20);
    put16(archive, 20);
    put16(archive, 0);
    put16(archive, entry.deflate ? 8 : 0);
    put16(archive, 0);
    put16(archive, 0);
    put32(archive, crc);
    put32(archive, static_cast<std::uint32_t>(payload.size()));
    put32(archive, static_cast<std::uint32_t>(entry.data.size()));
    put16(archive, static_cast<std::uint16_t>(entry.name.size()));
    put16(archive, 0);
    put16(archive, 0);
    put16(archive, 0);
    put16(archive, 0);
    put32(archive, 0);
    put32(archive, offsets[index]);
    archive.insert(archive.end(), entry.name.begin(), entry.name.end());
  }
  const std::uint32_t directorySize = static_cast<std::uint32_t>(archive.size()) - directoryOffset;
  put32(archive, 0x06054b50U);
  put16(archive, 0);
  put16(archive, 0);
  put16(archive, static_cast<std::uint16_t>(entries.size()));
  put16(archive, static_cast<std::uint16_t>(entries.size()));
  put32(archive, directorySize);
  put32(archive, directoryOffset);
  put16(archive, 0);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  output.write(reinterpret_cast<const char *>(archive.data()), static_cast<std::streamsize>(archive.size()));
  return static_cast<bool>(output);
}
#endif

} // namespace

int main(int, char **) {
  const fs::path homeDirectory = makeTemporaryDirectory("mrg-libretro-home-");
  expect(!homeDirectory.empty(), "cannot create a temporary data directory");
  if (homeDirectory.empty())
    return 1;
  const std::string home = homeDirectory.string();
  setTestEnvironment("XDG_DATA_HOME", home.c_str(), 1);
  setTestEnvironment("SDL_VIDEODRIVER", "dummy", 1);
  setTestEnvironment("SDL_AUDIODRIVER", "dummy", 1);
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "FAIL: SDL init: %s\n", SDL_GetError());
    return 1;
  }

  const std::string romPath = std::string(home) + "/stub.gba";
  {
    std::ofstream rom(romPath, std::ios::binary | std::ios::trunc);
    rom << "STUBGBA0";
  }
  expect(launcher::game::platformFromExtension(romPath) == launcher::game::GamePlatform::Gba,
         "a .gba file is recognised as a GBA game");
  std::string validationError;
  expect(launcher::game::validateGameFile(launcher::game::GamePlatform::Gba, romPath, validationError),
         "a .gba file passes import validation: " + validationError);

  launcher::library::GameEntry game;
  game.id = "stubgame";
  game.title = "Stub Game";
  game.filePath = romPath;
  game.platform = launcher::game::GamePlatform::Gba;

  launcher::runtime::RuntimeRegistry registry;
  expect(registry.supports(launcher::game::GamePlatform::Gba), "GBA is served by a libretro core");
  expect(registry.supports(launcher::game::GamePlatform::Nes), "NES is served by a libretro core");
  expect(!registry.supports(launcher::game::GamePlatform::Unknown), "an unknown platform is not served");

  SDL_Window *window = SDL_CreateWindow("libretro", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 320, 240,
                                        SDL_WINDOW_HIDDEN);
  expect(window != nullptr, "cannot create the test window");
  SDL_Renderer *renderer = window != nullptr ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : nullptr;
  expect(renderer != nullptr, "cannot create the test renderer");

  std::unique_ptr<launcher::runtime::IGameRuntime> runtime =
      registry.create(launcher::game::GamePlatform::Gba);
  expect(runtime != nullptr, "a GBA runtime can be created");
  if (runtime == nullptr)
    return 1;
  std::string error;
  expect(runtime->load(game, renderer, 80), "load failed: " + error);
  expect(runtime->error().empty(), "load left an error message");
  expect(std::string(runtime->name()) == "Stub GBA 1.0", "the core name is reported");
  expect(runtime->platform() == launcher::game::GamePlatform::Gba, "the platform is reported");

  for (int frame = 0; frame < 3; ++frame)
    runtime->stepFrame();
  runtime->render(renderer);

  // Core options: the stub declares two, the defaults are selected, and only
  // declared keys and values are accepted.
  const auto selectedOptionValue = [](const launcher::runtime::IGameRuntime &source,
                                      const std::string &key) {
    for (const auto &option : source.coreOptions()) {
      if (option.key == key && option.selected < option.values.size())
        return option.values[option.selected].value;
    }
    return std::string();
  };
  expect(runtime->coreOptions().size() == 2,
         "the core options were not collected");
  expect(selectedOptionValue(*runtime, "stub_brightness") == "medium",
         "the declared default is not selected");
  expect(selectedOptionValue(*runtime, "stub_audio") == "high",
         "the second option default is not selected");
  expect(runtime->setCoreOption("stub_brightness", "high"),
         "a declared option value is rejected");
  expect(selectedOptionValue(*runtime, "stub_brightness") == "high",
         "the selected value is not reported back");
  expect(!runtime->setCoreOption("stub_brightness", "purple"),
         "an undeclared value is accepted");
  expect(!runtime->setCoreOption("no_such_option", "x"),
         "an undeclared option is accepted");
  const fs::path optionFile =
      fs::path(launcher::config::dataSubdirectory("core-options", "")) /
      (fs::path(launcher::runtime::libretro::findCore(game.platform))
           .stem()
           .string() +
       ".tsv");
  std::error_code optionError;
  expect(fs::is_regular_file(optionFile, optionError),
         "the option value was not written to disk");

  expect(!runtime->stateInfo(0).present, "slot 0 starts empty");
  expect(runtime->loadState(2, error) == launcher::runtime::StateStatus::Missing, "an empty slot reports Missing");
  expect(runtime->saveState(0, error) == launcher::runtime::StateStatus::Saved, "save state failed: " + error);
  expect(runtime->statePresent(0), "slot 0 is present after saving");
  const launcher::runtime::StateInfo info = runtime->stateInfo(0);
  expect(info.present, "the state info marks the slot");
  expect(!info.savedAt.empty(), "the state info records a timestamp");
  expect(!info.playTime.empty(), "the state info records play time");
  expect(!info.thumbnailPath.empty(), "the state info records a thumbnail");
  expect(runtime->loadState(0, error) == launcher::runtime::StateStatus::Loaded, "load state failed: " + error);

  // Reloading a snapshot must land on exactly the same machine state, so a
  // freshly written second snapshot is byte-identical to the first.
  expect(runtime->saveState(1, error) == launcher::runtime::StateStatus::Saved, "second save state failed: " + error);
  const fs::path states = fs::path(launcher::config::dataSubdirectory("states", "gba"));
  std::vector<std::uint8_t> first;
  std::vector<std::uint8_t> second;
  const bool readFirst = readAll((states / "stubgame-slot1.mrgstate").string(), first);
  const bool readSecond = readAll((states / "stubgame-slot2.mrgstate").string(), second);
  expect(readFirst && readSecond, "cannot read the saved states");
  expect(first == second, "a reloaded state does not reproduce itself");

  // Rewind: stepping back has to restore an earlier picture, and the window has
  // to end instead of stepping past its oldest snapshot.
  const auto visibleFrame = [&]() -> int {
    std::uint32_t pixel = 0;
    const SDL_Rect centre{160, 120, 1, 1};
    if (SDL_RenderReadPixels(renderer, &centre, SDL_PIXELFORMAT_ARGB8888,
                             &pixel, sizeof(pixel)) != 0) {
      expect(false, "cannot read the rendered frame");
      return -1;
    }
    // The stub paints the frame counter into the blue channel.
    return static_cast<int>(pixel & 0xFFU);
  };
  runtime->setRewindEnabled(true);
  for (int frame = 0; frame < 10; ++frame)
    runtime->stepFrame();
  runtime->render(renderer);
  const int rewoundFrom = visibleFrame();
  expect(runtime->rewindStep(), "rewind did not step back");
  runtime->render(renderer);
  const int rewoundTo = visibleFrame();
  expect(rewoundFrom >= 0 && rewoundTo >= 0 && rewoundTo != rewoundFrom,
         "rewind did not restore an earlier frame");
  int rewoundSteps = 1;
  while (runtime->rewindStep())
    ++rewoundSteps;
  expect(rewoundSteps > 1, "the rewind window only held a single snapshot");
  expect(rewoundSteps < 10, "rewind kept more snapshots than frames were run");
  runtime->setRewindEnabled(false);
  expect(!runtime->rewindStep(), "rewind steps with the feature turned off");

  // Runahead: the picture shown has to be the one the core produces a few frames
  // beyond the state it keeps, which is what hides the core's own input lag.
  runtime->setRunaheadFrames(0);
  runtime->stepFrame();
  runtime->render(renderer);
  const int plainFrame = visibleFrame();
  runtime->setRunaheadFrames(2);
  runtime->stepFrame();
  runtime->render(renderer);
  const int aheadFrame = visibleFrame();
  expect(plainFrame >= 0 && aheadFrame >= 0 &&
             aheadFrame == (plainFrame + 3) % 256,
         "runahead did not show a frame two frames past the kept state");
  runtime->setRunaheadFrames(0);
  runtime->stepFrame();
  runtime->render(renderer);
  const int backToPlain = visibleFrame();
  expect(backToPlain >= 0 && backToPlain == (plainFrame + 2) % 256,
         "turning runahead off did not resume the normal frame sequence");

  runtime->powerCycle();
  expect(runtime->loadState(0, error) == launcher::runtime::StateStatus::Loaded,
         "load state after a power cycle failed: " + error);
  runtime->stop();

  const fs::path battery = fs::path(launcher::config::dataSubdirectory("saves", "gba")) / "stubgame.srm";
  std::error_code batteryError;
  expect(fs::is_regular_file(battery, batteryError) && !batteryError,
         "stopping the game did not write its battery save");
  expect(fs::file_size(battery, batteryError) == 128, "the battery save has the size the core reported");

  // The stored choice survives a reload and reaches the core through
  // GET_VARIABLE; resetting drops it and returns to the declared default.
  std::unique_ptr<launcher::runtime::IGameRuntime> resumed =
      registry.create(launcher::game::GamePlatform::Gba);
  expect(resumed != nullptr, "a second runtime can be created");
  if (resumed != nullptr) {
    expect(resumed->load(game, renderer, 80), "the second load failed");
    expect(selectedOptionValue(*resumed, "stub_brightness") == "high",
           "the stored option value was not restored");
    expect(resumed->setCoreOption("stub_audio", "low"),
           "a second option could not be changed");
    resumed->stepFrame();
    resumed->stop();
    std::vector<std::uint8_t> batteryBytes;
    expect(readAll(battery.string(), batteryBytes),
           "cannot read the battery save again");
    expect(batteryBytes.size() == 128, "the battery save changed size");
    expect(batteryBytes.size() > 1 &&
               batteryBytes[0] == static_cast<std::uint8_t>('h'),
           "the core did not receive the stored option value");
    // saveRam is an array of 16-bit words: byte 2 is the low half of the
    // flag the stub raises when GET_VARIABLE_UPDATE reports a change.
    expect(batteryBytes.size() > 2 && batteryBytes[2] == 1,
           "the core was not told that the options changed");

    expect(resumed->load(game, renderer, 80), "the third load failed");
    resumed->resetCoreOptions();
    expect(selectedOptionValue(*resumed, "stub_brightness") == "medium",
           "resetting did not restore the declared default");
    resumed->stop();
  }
  std::unique_ptr<launcher::runtime::IGameRuntime> fresh =
      registry.create(launcher::game::GamePlatform::Gba);
  expect(fresh != nullptr, "a third runtime can be created");
  if (fresh != nullptr) {
    expect(fresh->load(game, renderer, 80), "the fourth load failed");
    expect(selectedOptionValue(*fresh, "stub_brightness") == "medium",
           "the reset values were not persisted");
    fresh->stop();
  }

  // Without a core the same platform must fail with a message a player can act
  // on instead of a crash.
  const std::string previousSaves = rotateDirectory("MRG_CORES_DIR");
  expect(launcher::runtime::libretro::findCore(launcher::game::GamePlatform::Gba).empty(),
         "a core was found in an empty directory");
  std::unique_ptr<launcher::runtime::IGameRuntime> missing = registry.create(launcher::game::GamePlatform::Gba);
  expect(missing != nullptr, "the runtime is created without a core");
  if (missing != nullptr) {
    expect(!missing->load(game, renderer, 80), "load succeeded without an installed core");
    expect(std::string(missing->error()).find("libretro core") != std::string::npos,
           "the missing core error is not descriptive");
  }
  restoreDirectory("MRG_CORES_DIR", previousSaves);
  expect(!launcher::runtime::libretro::findCore(launcher::game::GamePlatform::Gba).empty(),
         "the installed core is not found again");

#if defined(MRG_HAVE_ZLIB)
  // Cores downloaded from the libretro buildbot arrive as .so.zip, so discovery
  // has to unpack one into the data directory and reuse it afterwards.
  const std::string installedCore = launcher::runtime::libretro::findCore(launcher::game::GamePlatform::Gba);
  std::vector<std::uint8_t> coreBytes;
  expect(readAll(installedCore, coreBytes), "cannot read the installed stub core");
  const std::string coreFileName = fs::path(installedCore).filename().string();

  const fs::path storedCores = fs::path(home) / "stored-cores";
  std::error_code zipError;
  fs::create_directories(storedCores, zipError);
  expect(writeZip(storedCores / (coreFileName + ".zip"), {{coreFileName, coreBytes, false}}),
         "cannot write a stored zip");
  setTestEnvironment("MRG_CORES_DIR", storedCores, 1);
  const std::string unpacked = launcher::runtime::libretro::findCore(launcher::game::GamePlatform::Gba);
  expect(!unpacked.empty(), "a zipped core is not discovered");
  expect(fs::path(unpacked).filename().string() == coreFileName, "the unpacked core keeps its name");
  expect(unpacked.find("cores-unpacked") != std::string::npos, "the core is not unpacked into the data directory");
  expect(launcher::runtime::libretro::findCore(launcher::game::GamePlatform::Gba) == unpacked,
         "an already unpacked core is not reused");
  std::unique_ptr<launcher::runtime::IGameRuntime> unpackedRuntime = registry.create(launcher::game::GamePlatform::Gba);
  expect(unpackedRuntime != nullptr, "a runtime cannot be created from a zipped core");
  if (unpackedRuntime != nullptr) {
    expect(unpackedRuntime->load(game, renderer, 80),
           "load from a zipped core failed: " + std::string(unpackedRuntime->error()));
    unpackedRuntime->stepFrame();
    unpackedRuntime->render(renderer);
    unpackedRuntime->stop();
  }

  // The inflate path only runs when the extracted copy is gone, so drop it and
  // unpack the same core from a deflated archive instead.
  const fs::path deflatedCores = fs::path(home) / "deflated-cores";
  fs::create_directories(deflatedCores, zipError);
  expect(writeZip(deflatedCores / (coreFileName + ".zip"), {{coreFileName, coreBytes, true}}),
         "cannot write a deflated zip");
  fs::remove(unpacked, zipError);
  setTestEnvironment("MRG_CORES_DIR", deflatedCores, 1);
  const std::string inflated = launcher::runtime::libretro::findCore(launcher::game::GamePlatform::Gba);
  expect(!inflated.empty(), "a deflated core is not unpacked");
  std::vector<std::uint8_t> inflatedBytes;
  expect(readAll(inflated, inflatedBytes) && inflatedBytes == coreBytes,
         "the inflated core differs from the archive content");

  // A file that is not a zip must not be mistaken for an installed core.
  const fs::path corruptCores = fs::path(home) / "corrupt-cores";
  fs::create_directories(corruptCores, zipError);
  {
    std::ofstream bogus(corruptCores / (coreFileName + ".zip"), std::ios::binary | std::ios::trunc);
    bogus << "not a zip archive";
  }
  setTestEnvironment("MRG_CORES_DIR", corruptCores, 1);
  expect(launcher::runtime::libretro::findCore(launcher::game::GamePlatform::Gba).empty(),
         "a corrupt archive is accepted as a core");

  // Removal deletes the library itself, so the platform reads as uninstalled
  // and a second removal reports that there is nothing left to delete.
  const fs::path removalCores = fs::path(home) / "removal-cores";
  fs::create_directories(removalCores, zipError);
  {
    std::ofstream copy(removalCores / coreFileName,
                       std::ios::binary | std::ios::trunc);
    copy.write(reinterpret_cast<const char *>(coreBytes.data()),
               static_cast<std::streamsize>(coreBytes.size()));
  }
  setTestEnvironment("MRG_CORES_DIR", removalCores, 1);
  expect(launcher::runtime::libretro::findCore(
             launcher::game::GamePlatform::Gba) ==
             (removalCores / coreFileName).string(),
         "the copied core is not discovered before removal");
  std::string removalError;
  expect(launcher::runtime::libretro::removeInstalledCore(
             launcher::game::GamePlatform::Gba, removalError),
         "cannot remove an installed core: " + removalError);
  expect(!fs::exists(removalCores / coreFileName, zipError),
         "the removed core is still on disk");
  expect(launcher::runtime::libretro::findCore(
             launcher::game::GamePlatform::Gba).empty(),
         "a removed core is still discovered");
  removalError.clear();
  expect(!launcher::runtime::libretro::removeInstalledCore(
             launcher::game::GamePlatform::Gba, removalError),
         "removing a missing core reports success");
  expect(!removalError.empty(), "a failed removal explains nothing");
  restoreDirectory("MRG_CORES_DIR", previousSaves);
#endif

  // NES is routed through libretro like every other platform, so a NES core on
  // disk must be discovered and used.
  expect(!launcher::runtime::libretro::findCore(launcher::game::GamePlatform::Nes).empty(),
         "the installed NES core is not discovered");
  const std::string nesPath = std::string(home) + "/stub.nes";
  {
    std::ofstream rom(nesPath, std::ios::binary | std::ios::trunc);
    rom << "NES\x1aSTUB";
  }
  launcher::library::GameEntry nesGame;
  nesGame.id = "stubnes";
  nesGame.title = "Stub NES";
  nesGame.filePath = nesPath;
  nesGame.platform = launcher::game::GamePlatform::Nes;
  std::unique_ptr<launcher::runtime::IGameRuntime> nesRuntime =
      registry.create(launcher::game::GamePlatform::Nes);
  expect(nesRuntime != nullptr, "a NES runtime is created");
  if (nesRuntime != nullptr) {
    std::string nesError;
    expect(nesRuntime->load(nesGame, renderer, 80), "NES load failed: " + std::string(nesRuntime->error()));
    nesRuntime->stepFrame();
    nesRuntime->render(renderer);
    expect(nesRuntime->saveState(0, nesError) == launcher::runtime::StateStatus::Saved,
           "NES save state failed: " + nesError);
    nesRuntime->stop();
  }

  // A game whose path is not ASCII also has to load, snapshot and save its
  // battery RAM, all of which reach the filesystem as strings.
  {
    const fs::path unicodeRom =
        homeDirectory / launcher::paths::fromUtf8("超级马里奥.gba");
    {
      std::ofstream rom(unicodeRom, std::ios::binary | std::ios::trunc);
      rom << "STUBGBA0";
    }
    launcher::library::GameEntry unicodeGame;
    unicodeGame.id = "超级马里奥";
    unicodeGame.title = "超级马里奥";
    unicodeGame.filePath = launcher::paths::toUtf8(unicodeRom);
    unicodeGame.platform = launcher::game::GamePlatform::Gba;
    std::unique_ptr<launcher::runtime::IGameRuntime> unicodeRuntime =
        registry.create(launcher::game::GamePlatform::Gba);
    expect(unicodeRuntime != nullptr,
           "no runtime can be created for a non-ASCII game");
    if (unicodeRuntime != nullptr) {
      std::string unicodeError;
      expect(unicodeRuntime->load(unicodeGame, renderer, 80),
             "loading a non-ASCII path failed: " +
                 std::string(unicodeRuntime->error()));
      unicodeRuntime->stepFrame();
      unicodeRuntime->render(renderer);
      expect(unicodeRuntime->saveState(0, unicodeError) ==
                 launcher::runtime::StateStatus::Saved,
             "saving a non-ASCII state failed: " + unicodeError);
      const launcher::runtime::StateInfo unicodeInfo =
          unicodeRuntime->stateInfo(0);
      expect(unicodeInfo.present, "the non-ASCII state slot is not present");
      expect(!unicodeInfo.thumbnailPath.empty() &&
                 fs::is_regular_file(
                     launcher::paths::fromUtf8(unicodeInfo.thumbnailPath)),
             "the non-ASCII state thumbnail is missing");
      SDL_Surface *unicodeThumbnail =
          launcher::io::loadSurfaceBmp(unicodeInfo.thumbnailPath);
      expect(unicodeThumbnail != nullptr,
             "the non-ASCII state thumbnail cannot be decoded");
      if (unicodeThumbnail != nullptr)
        SDL_FreeSurface(unicodeThumbnail);
      expect(unicodeRuntime->loadState(0, unicodeError) ==
                 launcher::runtime::StateStatus::Loaded,
             "loading a non-ASCII state failed: " + unicodeError);
      unicodeRuntime->stop();
      const fs::path unicodeBattery =
          fs::path(launcher::config::dataSubdirectory("saves", "gba")) /
          launcher::paths::fromUtf8("超级马里奥.srm");
      expect(fs::is_regular_file(unicodeBattery),
             "the non-ASCII battery save is missing");
    }
  }

  // The installer resolves a core name against a base URL; nothing touches the
  // network unless MRG_TEST_CORE_INSTALL is set below.
  const std::vector<launcher::runtime::libretro::CorePackage> &packages =
      launcher::runtime::libretro::corePackages();
  expect(!packages.empty(), "no core packages are configured for this platform");
  if (!packages.empty()) {
    const std::string suffix = launcher::runtime::libretro::coreLibrarySuffix();
    expect(suffix == ".so" || suffix == ".dylib" || suffix == ".dll", "unexpected core library suffix");
    expect(launcher::runtime::libretro::coreLibraryName(packages.front()) ==
               std::string(packages.front().core) + suffix,
           "the installed library name misses its suffix");
    const std::string url = launcher::runtime::libretro::coreArchiveUrl(packages.front());
    expect(url.size() > 4 && url.compare(url.size() - 4, 4, ".zip") == 0, "the archive URL misses its zip suffix");
    expect(url.find("/latest/") != std::string::npos, "the archive URL is not a nightly latest build");
    expect(url.find(packages.front().core) != std::string::npos, "the archive URL misses the core name");
    // A mirror that cannot be reached has to fail with a message instead of
    // hanging or pretending to succeed.
    const std::string previousBase = launcher::runtime::libretro::coreArchiveBaseUrl();
    setTestEnvironment("MRG_CORES_BASE_URL", "http://127.0.0.1:1/unreachable", 1);
    const launcher::runtime::libretro::InstallOutcome failed =
        launcher::runtime::libretro::installCore(packages.front(), (fs::path(home) / "failed-cores").string());
    expect(!failed.ok, "installCore accepted an unreachable mirror");
    expect(!failed.message.empty(), "a failed install reports nothing");
    clearTestEnvironment("MRG_CORES_BASE_URL");
    expect(launcher::runtime::libretro::coreArchiveBaseUrl() == previousBase, "the mirror override leaked");
  }
  // Opt-in: the installer downloads from the libretro buildbot, so the real
  // fetch only runs when MRG_TEST_CORE_INSTALL is set and the network is up.
  const char *installCheck = std::getenv("MRG_TEST_CORE_INSTALL");
  if (installCheck != nullptr && installCheck[0] != '\0') {
    expect(!packages.empty(), "no core packages are configured for this platform");
    if (!packages.empty()) {
      const fs::path target = fs::path(home) / "installed-cores";
      const launcher::runtime::libretro::InstallOutcome outcome =
          launcher::runtime::libretro::installCore(packages.front(), target.string());
      expect(outcome.ok, "installCore failed: " + outcome.message);
      if (outcome.ok)
        expect(fs::is_regular_file(fs::path(outcome.message)), "the installed core is not on disk");
    }
  }

  if (renderer != nullptr)
    SDL_DestroyRenderer(renderer);
  if (window != nullptr)
    SDL_DestroyWindow(window);
  SDL_Quit();
  std::error_code ignored;
  fs::remove_all(home, ignored);

  if (failures != 0) {
    std::fprintf(stderr, "%d libretro runtime checks failed\n", failures);
    return 1;
  }
  std::printf("libretro runtime checks passed\n");
  return 0;
}
