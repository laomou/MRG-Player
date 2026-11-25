#include "app/Application.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/LaunchOptions.hpp"
#include "config/AppPaths.hpp"
#include "config/InputBindings.hpp"
#include "config/Settings.hpp"
#include "i18n/Localization.hpp"
#include "library/GameLibrary.hpp"
#include "model/Game.hpp"
#include "platform/RomFilePicker.hpp"
#include "runtime/IGameRuntime.hpp"
#include "runtime/RuntimeRegistry.hpp"
#include "runtime/libretro/CoreDiscovery.hpp"
#include "runtime/libretro/CoreInstaller.hpp"
#include "util/ImageFile.hpp"
#include "util/Utf8Path.hpp"
#include "ui/CoverCache.hpp"
#include "ui/GameManagementScreen.hpp"
#include "ui/HomeScreen.hpp"
#include "ui/PauseMenuScreen.hpp"
#include "ui/Primitives.hpp"
#include "ui/SettingsScreen.hpp"
#include "ui/Theme.hpp"
#include "util/PngWriter.hpp"

namespace launcher {
namespace {

enum class Screen { Home, Settings, Management, Game, Pause };

// One install at a time, run on an SDL thread so a slow network never freezes
// the launcher; the frame loop polls the flag and joins the thread.
struct CoreInstallJob {
  runtime::libretro::CorePackage package;
  std::string directory;
  std::atomic<bool> *done = nullptr;
  bool *ok = nullptr;
  std::string *message = nullptr;
};

int runCoreInstall(void *data) {
  auto *job = static_cast<CoreInstallJob *>(data);
  const runtime::libretro::InstallOutcome outcome =
      runtime::libretro::installCore(job->package, job->directory);
  if (job->ok != nullptr)
    *job->ok = outcome.ok;
  if (job->message != nullptr)
    *job->message = outcome.message;
  if (job->done != nullptr)
    job->done->store(true);
  delete job;
  return 0;
}

constexpr game::GamePlatform kInstallerPlatforms[] = {
    game::GamePlatform::Nes,   game::GamePlatform::Gba,
    game::GamePlatform::Snes,  game::GamePlatform::GameBoy,
    game::GamePlatform::Genesis};

// A core library is a few megabytes, so one decimal keeps the installer line
// readable without the row growing with the download.
std::string formatCoreSize(std::uintmax_t bytes) {
  constexpr double kBytesPerKib = 1024.0;
  char buffer[32] = {};
  if (bytes >= 1024U * 1024U)
    std::snprintf(buffer, sizeof(buffer), "%.1f MB",
                  static_cast<double>(bytes) / (kBytesPerKib * kBytesPerKib));
  else
    std::snprintf(buffer, sizeof(buffer), "%.0f KB",
                  static_cast<double>(bytes) / kBytesPerKib);
  return buffer;
}

struct ControllerCandidate {
  int deviceIndex = -1;
  SDL_JoystickID instanceId = -1;
  std::string guid;
  std::string name;
};

std::string controllerGuidAt(int deviceIndex) {
  char value[33]{};
  SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(deviceIndex), value,
                            sizeof(value));
  return value;
}

std::vector<ControllerCandidate> availableControllers() {
  std::vector<ControllerCandidate> result;
  for (int index = 0; index < SDL_NumJoysticks(); ++index) {
    if (!SDL_IsGameController(index))
      continue;
    const char *name = SDL_GameControllerNameForIndex(index);
    result.push_back({index, SDL_JoystickGetDeviceInstanceID(index),
                      controllerGuidAt(index),
                      name != nullptr ? name : "Controller"});
  }
  return result;
}

SDL_GameController *openPreferredController(std::string_view preferredGuid) {
  const auto candidates = availableControllers();
  for (const auto &candidate : candidates) {
    if (!preferredGuid.empty() && candidate.guid == preferredGuid)
      return SDL_GameControllerOpen(candidate.deviceIndex);
  }
  return candidates.empty()
             ? nullptr
             : SDL_GameControllerOpen(candidates.front().deviceIndex);
}

std::string openedControllerGuid(SDL_GameController *controller) {
  if (controller == nullptr)
    return {};
  char value[33]{};
  SDL_JoystickGetGUIDString(
      SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(controller)), value,
      sizeof(value));
  return value;
}

#if defined(MRG_ENABLE_SMOKE_TESTS)
void captureFrame(SDL_Renderer *renderer, const char *outputPath) {
  int width = 0;
  int height = 0;
  SDL_GetRendererOutputSize(renderer, &width, &height);
  SDL_Surface *capture = SDL_CreateRGBSurfaceWithFormat(
      0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
  if (capture == nullptr)
    return;
  SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                       capture->pixels, capture->pitch);
  SDL_SaveBMP(capture, outputPath);
  SDL_FreeSurface(capture);
}
#endif

std::string timestampedScreenshotName() {
  const std::time_t now = std::time(nullptr);
  std::tm parts{};
#if defined(_WIN32)
  localtime_s(&parts, &now);
#else
  localtime_r(&now, &parts);
#endif
  char buffer[32]{};
  std::strftime(buffer, sizeof(buffer), "mrg-%Y%m%d-%H%M%S", &parts);
  return buffer;
}

// Saves the current frame as <data>/screenshots/mrg-<stamp>.png and falls back
// to BMP when the build has no zlib. Returns the written path, or an empty
// string with "error" set.
std::string saveFrameToFile(SDL_Renderer *renderer, std::string &error) {
  error.clear();
  int width = 0;
  int height = 0;
  if (renderer == nullptr ||
      SDL_GetRendererOutputSize(renderer, &width, &height) != 0 || width <= 0 ||
      height <= 0) {
    error = renderer == nullptr ? "no renderer" : SDL_GetError();
    return {};
  }
  SDL_Surface *capture = SDL_CreateRGBSurfaceWithFormat(
      0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
  if (capture == nullptr) {
    error = SDL_GetError();
    return {};
  }
  if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                           capture->pixels, capture->pitch) != 0) {
    error = SDL_GetError();
    SDL_FreeSurface(capture);
    return {};
  }

  std::error_code ec;
  const std::filesystem::path directory =
      paths::fromUtf8(config::dataRoot()) / "screenshots";
  std::filesystem::create_directories(directory, ec);
  if (ec) {
    error = ec.message();
    SDL_FreeSurface(capture);
    return {};
  }
  const std::string base = timestampedScreenshotName();

  if (io::pngWritingAvailable()) {
    // SDL hands back ARGB8888, which is BGRA byte order on little-endian.
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
        4U);
    const auto *source = static_cast<const std::uint8_t *>(capture->pixels);
    for (int row = 0; row < height; ++row) {
      const std::uint8_t *line =
          source + static_cast<std::size_t>(row) *
                       static_cast<std::size_t>(capture->pitch);
      std::uint8_t *target =
          rgba.data() + static_cast<std::size_t>(row) *
                            static_cast<std::size_t>(width) * 4U;
      for (int column = 0; column < width; ++column) {
        target[0] = line[2];
        target[1] = line[1];
        target[2] = line[0];
        target[3] = 255;
        line += 4;
        target += 4;
      }
    }
    const std::string path = paths::toUtf8(directory / (base + ".png"));
    if (io::writePngRgba(path, width, height, rgba.data(),
                         static_cast<std::size_t>(width) * 4U)) {
      SDL_FreeSurface(capture);
      return path;
    }
    error = "PNG encoding failed";
  }

  const std::string path = paths::toUtf8(directory / (base + ".bmp"));
  const bool saved = SDL_SaveBMP(capture, path.c_str()) == 0;
  if (!saved && error.empty())
    error = SDL_GetError();
  SDL_FreeSurface(capture);
  return saved ? path : std::string();
}

} // namespace

int Application::run(int argc, char **argv) {
  LaunchOptions options = parseLaunchOptions(argc, argv);
  if (!options.error.empty()) {
    std::fprintf(stderr, "%s\n", options.error.c_str());
    return 1;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) !=
      0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    std::fprintf(stderr, "SDL audio unavailable; continuing silently: %s\n",
                 SDL_GetError());
  }

  AppSettings settings;
  loadSettings(settings);
  if (options.languageOverride >= 0)
    settings.language = options.languageOverride;
  char *basePath = SDL_GetBasePath();
  const std::string executablePath = basePath != nullptr ? basePath : "";
  SDL_free(basePath);
  std::string fontPath =
      executablePath + "assets/fonts/DroidSansFallbackFull.ttf";
  if (!ui::initializeTextRenderer(fontPath)) {
    fontPath =
        executablePath + "../share/mrg-player/fonts/DroidSansFallbackFull.ttf";
    if (!ui::initializeTextRenderer(fontPath)) {
      std::fprintf(
          stderr,
          "Failed to initialize multilingual text renderer from %s: %s\n",
          fontPath.c_str(), SDL_GetError());
      ui::shutdownTextRenderer();
      SDL_Quit();
      return 1;
    }
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_Window *window = SDL_CreateWindow(
      "MRG Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      ui::kLogicalWidth, ui::kLogicalHeight,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (window == nullptr) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    ui::shutdownTextRenderer();
    SDL_Quit();
    return 1;
  }
  if (settings.fullscreen && !options.smokeTest()) {
    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == nullptr)
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (renderer == nullptr) {
    std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    ui::shutdownTextRenderer();
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  SDL_GameController *controller =
      openPreferredController(settings.preferredControllerGuid);
  SDL_GameController *controller2 = nullptr;
  auto instanceIdOf = [](SDL_GameController *pad) -> SDL_JoystickID {
    return pad != nullptr
               ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))
               : 0;
  };
  runtime::RuntimeRegistry runtimeRegistry;
  std::unique_ptr<runtime::IGameRuntime> activeRuntime;

  // Port 1 takes the preferred pad; port 2 gets the next free pad so two
  // people can play without touching the settings.
  auto openSecondController = [&] {
    if (controller2 != nullptr)
      return;
    const SDL_JoystickID firstId = instanceIdOf(controller);
    const auto candidates = availableControllers();
    int chosen = -1;
    for (const auto &candidate : candidates) {
      if (candidate.instanceId == firstId)
        continue;
      if (!settings.preferredControllerGuid2.empty() &&
          candidate.guid == settings.preferredControllerGuid2) {
        chosen = candidate.deviceIndex;
        break;
      }
      if (chosen < 0)
        chosen = candidate.deviceIndex;
    }
    if (chosen < 0)
      return;
    controller2 = SDL_GameControllerOpen(chosen);
    if (controller2 != nullptr)
      settings.preferredControllerGuid2 = openedControllerGuid(controller2);
  };
  auto syncPlayerControllers = [&] {
    if (!activeRuntime)
      return;
    activeRuntime->setPlayerController(0, instanceIdOf(controller));
    activeRuntime->setPlayerController(1, instanceIdOf(controller2));
  };
  // A game may carry its own display override, so the effective options are
  // resolved once into activeVideo and every other path just re-applies them.
  runtime::VideoOptions activeVideo;
  auto syncVideoOptions = [&] {
    if (!activeRuntime)
      return;
    activeRuntime->setVideoOptions(activeVideo);
  };
  auto syncAudioOptions = [&] {
    if (!activeRuntime)
      return;
    activeRuntime->setMuted(settings.audioMuted);
    activeRuntime->setFastForwardAudio(settings.fastForwardAudio != 0);
  };
  auto syncPlaybackOptions = [&] {
    if (!activeRuntime)
      return;
    activeRuntime->setRewindEnabled(settings.rewind);
    activeRuntime->setRunaheadFrames(settings.runahead);
  };
  openSecondController();
  library::GameLibrary gameLibrary;
  if (!gameLibrary.initialize()) {
    std::fprintf(stderr, "Game library unavailable: %s\n",
                 std::string(gameLibrary.error()).c_str());
  }
  // Box art lives next to the saves, beside a names.json the player can edit.
  ui::CoverCache coverCache;
  coverCache.initialize(config::dataSubdirectory("covers", ""));
  bool running = true;
  bool startupRomFailed = false;
  Screen screen = options.smokeSettings ? Screen::Settings : Screen::Home;
  int selectedGame = settings.rememberSelection ? settings.selectedGame : 0;
  ui::HomeFocus homeFocus = ui::HomeFocus::Cards;
  int homePill = 0;
  int homeAction = 0;
  float homeRow = 0.0f;
  bool homeRowSettled = false;
  // Which half of the settings screen the direction keys drive: the category
  // rail or the page next to it. Entering the screen always starts on the rail.
  bool settingsRailFocus = true;
  // The focus ring fades in when the keys land on a different control; the key
  // below changes whenever that happens, so holding a direction does not
  // restart the fade and changing a value never replays it.
  std::string focusKey;
  float focusOpacity = 1.0f;
  Uint64 focusSince = 0;
  selectedGame = std::clamp(selectedGame, 0,
                            static_cast<int>(gameLibrary.slotCount()) - 1);
  int settingCategory = 0;
  int settingRow = 0;
  bool rewindHeld = false;
#if defined(MRG_ENABLE_SMOKE_TESTS)
  const Uint64 smokeSettingsStart = SDL_GetTicks64();
  bool smokeSettingsGeneral = false;
  bool smokeSettingsDisplay = false;
  bool smokeSettingsAudio = false;
  bool smokeSettingsController = false;
  bool smokeHomePills = false;
  bool smokeHomeActions = false;
  bool smokeHomeQuit = false;
  bool smokeHomeDone = false;
  bool smokeSettingsCores = false;
  bool smokeSettingsTheme = false;
  bool smokeSettingsAbout = false;
#endif
  std::vector<ui::CoreRow> coreRows;
  std::atomic<bool> coreInstallBusy{false};
  std::atomic<bool> coreInstallDone{false};
  bool coreInstallOk = false;
  std::string coreInstallMessage;
  SDL_Thread *coreInstallThread = nullptr;
  int coreInstallRow = 0;
  bool bindingCapture = false;
  // Closing the launcher asks first, the way a console does; 0 keeps the
  // cancel button focused so a stray key never ends the session.
  bool quitConfirm = false;
  int quitChoice = 0;
  int managementAction = 0;
  int confirmationChoice = 0;
  ui::ManagementDialog managementDialog = ui::ManagementDialog::Menu;
  bool saveDataPresent = false;
  int pauseAction = 0;
#if defined(MRG_ENABLE_SMOKE_TESTS)
  bool smokePauseCard = false;
  int smokeRomFrames = 0;
  bool smokeRomCaptured = false;
  bool smokePauseVideo = false;
  bool smokePauseCoreOptions = false;
#endif
  int stateSlot = 0;
  bool pauseConfirmingPower = false;
  int pausePowerChoice = 0;
  // The display panel takes over the pause card; its values are previewed on
  // the running game and only written to the library when it is enabled.
  bool pauseVideoPanel = false;
  int pauseVideoRow = 0;
  ui::PauseVideoState pauseVideo;
  // The core-options panel lists whatever the running core declared; values
  // are stored per core so every game on that core keeps them.
  bool pauseCoreOptionsPanel = false;
  int pauseCoreOptionCursor = 0;
  int pauseCoreOptionScroll = 0;
  std::string pauseFeedback;
  Uint64 pauseFeedbackUntil = 0;
  // Short banner over the picture when the mute hotkey changes the setting.
  std::string bannerMessage;
  Uint64 bannerUntil = 0;
  bool screenshotRequested = false;
  // Thumbnails and timestamps for the pause menu, refreshed when a snapshot is
  // written or the pause menu opens.
  std::array<ui::StateSlotView, ui::kStateSlotCount> stateSlots{};
  auto clearStateSlots = [&] {
    for (auto &slot : stateSlots) {
      if (slot.thumbnail != nullptr) {
        SDL_DestroyTexture(slot.thumbnail);
        slot.thumbnail = nullptr;
      }
      slot = ui::StateSlotView{};
    }
  };
  auto refreshStateSlots = [&] {
    clearStateSlots();
    for (int slot = 0; slot < ui::kStateSlotCount; ++slot) {
      const runtime::StateInfo info =
          activeRuntime ? activeRuntime->stateInfo(slot) : runtime::StateInfo{};
      stateSlots[static_cast<std::size_t>(slot)].present = info.present;
      stateSlots[static_cast<std::size_t>(slot)].savedAt = info.savedAt;
      stateSlots[static_cast<std::size_t>(slot)].playTime = info.playTime;
      if (info.thumbnailPath.empty())
        continue;
      SDL_Surface *surface = io::loadSurfaceBmp(info.thumbnailPath);
      if (surface == nullptr)
        continue;
      stateSlots[static_cast<std::size_t>(slot)].thumbnail =
          SDL_CreateTextureFromSurface(renderer, surface);
      SDL_FreeSurface(surface);
    }
  };
  std::string activeGameId;
  std::string activeGameTitle;
  game::GamePlatform activeGamePlatform = game::GamePlatform::Unknown;
  auto globalVideoOptions = [&] {
    runtime::VideoOptions video;
    video.aspect = static_cast<runtime::AspectMode>(
        std::clamp(settings.videoAspect, 0, 2));
    video.integerScale = settings.videoIntegerScale;
    video.overscan =
        std::clamp(settings.videoOverscan, 0, runtime::kMaxOverscan);
    return video;
  };
  // A per-game override wins; games without one follow the global settings.
  auto gameVideoOptions = [&]() {
    runtime::VideoOptions video = globalVideoOptions();
    if (!activeGameId.empty())
      (void)gameLibrary.videoOverride(activeGameId, activeGamePlatform, video);
    return video;
  };
  auto refreshActiveVideo = [&] { activeVideo = gameVideoOptions(); };
  Uint64 sessionActiveSince = 0;
  Uint64 sessionAccumulatedMs = 0;
  Uint64 toastUntil = 0;
  std::string toastMessage;
  Uint64 lastAxisMove = 0;
  config::InputDeviceKind lastInputDevice =
      config::InputDeviceKind::KeyboardMouse;
  const Uint64 start = SDL_GetPerformanceCounter();
  const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());

  auto controllerLabel = [&](SDL_GameController *pad) -> std::string {
    if (pad == nullptr)
      return {};
    const char *rawName = SDL_GameControllerName(pad);
    std::string name = rawName != nullptr ? rawName : "Controller";
    const auto candidates = availableControllers();
    if (candidates.size() <= 1)
      return name;
    const SDL_JoystickID instance = instanceIdOf(pad);
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      if (candidates[index].instanceId == instance)
        return name + " (" + std::to_string(index + 1) + "/" +
               std::to_string(candidates.size()) + ")";
    }
    return name;
  };
  auto activeControllerName = [&]() -> std::string {
    if (controller == nullptr && controller2 == nullptr)
      return {};
    const std::string first = controllerLabel(controller);
    const std::string second = controllerLabel(controller2);
    std::string combined = "P1 " + (first.empty() ? std::string("--") : first);
    if (!second.empty())
      combined += "   P2 " + second;
    return combined;
  };
  auto resolvedControllerStyle = [&] {
    const SDL_GameControllerType detected =
        controller != nullptr ? SDL_GameControllerGetType(controller)
                              : SDL_CONTROLLER_TYPE_UNKNOWN;
    return config::resolveControllerStyle(settings.controllerStyle, detected);
  };
  auto currentPrompts = [&] {
    const config::InputDeviceKind device = config::resolvePromptDevice(
        settings.promptDevice, lastInputDevice, controller != nullptr);
    return config::inputPrompts(device, resolvedControllerStyle());
  };
  // Cycling assigns the pad for the port currently being edited, skipping the
  // pad that already belongs to the other player.
  auto cycleController = [&](int direction) {
    const int player = std::clamp(settings.bindingsPlayer, 0, 1);
    SDL_GameController *&slot = player == 0 ? controller : controller2;
    const SDL_JoystickID otherId =
        instanceIdOf(player == 0 ? controller2 : controller);
    std::vector<ControllerCandidate> usable;
    for (const auto &candidate : availableControllers())
      if (candidate.instanceId != otherId)
        usable.push_back(candidate);
    if (usable.empty())
      return;
    const SDL_JoystickID currentInstance = instanceIdOf(slot);
    int currentIndex = -1;
    for (std::size_t index = 0; index < usable.size(); ++index)
      if (usable[index].instanceId == currentInstance)
        currentIndex = static_cast<int>(index);
    const int count = static_cast<int>(usable.size());
    const int nextIndex =
        currentIndex < 0 ? (direction > 0 ? 0 : count - 1)
                         : ((currentIndex + direction) % count + count) % count;
    if (activeRuntime)
      activeRuntime->releaseInput();
    if (slot != nullptr)
      SDL_GameControllerClose(slot);
    slot = SDL_GameControllerOpen(
        usable[static_cast<std::size_t>(nextIndex)].deviceIndex);
    if (slot != nullptr) {
      (player == 0 ? settings.preferredControllerGuid
                   : settings.preferredControllerGuid2) =
          openedControllerGuid(slot);
    }
    syncPlayerControllers();
  };

  auto applySetting = [&](int direction) {
    if (settingCategory == 3 && settingRow == 2)
      cycleController(direction);
    else
      ui::changeSetting(settings, settingCategory, settingRow, direction);
    if (settingCategory == 1) {
      SDL_SetWindowFullscreen(
          window, settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
    if (activeRuntime) {
      activeRuntime->setVolume(settings.volume);
      activeRuntime->setInputBindings(settings.input);
    }
    refreshActiveVideo();
    syncVideoOptions();
    syncAudioOptions();
    syncPlaybackOptions();
    saveSettings(settings);
  };
  auto currentLanguage = [&] {
    return i18n::languageFromSetting(settings.language);
  };
  auto showToast = [&](i18n::TextId id, Uint64 duration = 2800) {
    toastMessage = i18n::tr(id, currentLanguage());
    toastUntil = SDL_GetTicks64() + duration;
  };
  // The installer page reads the disk, so it is refreshed when it is opened, when
  // an install finishes, and on demand.
  auto refreshCoreRows = [&] {
    coreRows.clear();
    for (game::GamePlatform platform : kInstallerPlatforms) {
      ui::CoreRow entry;
      entry.platform = std::string(game::platformDescriptor(platform).displayName);
      const std::vector<std::string> candidates =
          runtime::libretro::coreCandidates(platform);
      if (!candidates.empty())
        entry.core = candidates.front();
      const std::string installed = runtime::libretro::findCore(platform);
      entry.installed = !installed.empty();
      if (entry.installed) {
        entry.core = paths::toUtf8(paths::fromUtf8(installed).filename());
        std::error_code sizeError;
        const std::uintmax_t bytes =
            std::filesystem::file_size(paths::fromUtf8(installed), sizeError);
        if (!sizeError && bytes > 0)
          entry.size = formatCoreSize(bytes);
      }
      const bool downloading =
          coreInstallBusy && coreInstallRow == static_cast<int>(coreRows.size());
      entry.status = i18n::tr(
          downloading ? i18n::TextId::CoreDownloading
                      : (entry.installed ? i18n::TextId::CoreInstalled
                                         : i18n::TextId::CoreNotInstalled),
          currentLanguage());
      coreRows.push_back(entry);
    }
  };
  auto startCoreInstall = [&](int row) {
    if (coreInstallBusy || row < 0 ||
        row >= static_cast<int>(std::size(kInstallerPlatforms))) {
      return;
    }
    const runtime::libretro::CorePackage *chosen = nullptr;
    for (const runtime::libretro::CorePackage &package :
         runtime::libretro::corePackages()) {
      if (package.platform == kInstallerPlatforms[row]) {
        chosen = &package;
        break;
      }
    }
    const std::string root = config::dataRoot();
    if (chosen == nullptr || root.empty()) {
      showToast(i18n::TextId::CoreInstallFailedBanner);
      return;
    }
    auto *job = new CoreInstallJob{
        *chosen, paths::toUtf8(paths::fromUtf8(root) / "cores"),
        &coreInstallDone, &coreInstallOk, &coreInstallMessage};
    coreInstallBusy = true;
    coreInstallDone = false;
    coreInstallRow = row;
    coreInstallMessage.clear();
    coreInstallThread = SDL_CreateThread(runCoreInstall, "core-install", job);
    refreshCoreRows();
  };
  auto changeCategory = [&](int direction) {
    const int count = ui::settingCategoryCount();
    settingCategory = (settingCategory + direction + count) % count;
    settingRow = 0;
    if (settingCategory == 5)
      refreshCoreRows();
  };
  // Opening the settings screen always re-reads the installed cores, so the
  // page is populated whether it was reached by key, pad, or mouse.
  auto openSettings = [&] {
    refreshCoreRows();
    settingsRailFocus = true;
    screen = Screen::Settings;
  };
  auto pauseSessionClock = [&] {
    if (sessionActiveSince != 0) {
      sessionAccumulatedMs += SDL_GetTicks64() - sessionActiveSince;
      sessionActiveSince = 0;
    }
  };
  auto resumeSessionClock = [&] {
    if (sessionActiveSince == 0)
      sessionActiveSince = SDL_GetTicks64();
  };
  auto recordCurrentSession = [&] {
    pauseSessionClock();
    if (!activeGameId.empty()) {
      const std::uint64_t seconds = sessionAccumulatedMs / 1000;
      if (!gameLibrary.recordPlaySession(
              activeGameId, activeGamePlatform, seconds,
              static_cast<std::int64_t>(std::time(nullptr)))) {
        std::fprintf(stderr, "Metadata update failed: %s\n",
                     std::string(gameLibrary.error()).c_str());
      }
      const int refreshed =
          gameLibrary.indexOfId(activeGameId, activeGamePlatform);
      if (refreshed >= 0)
        selectedGame = refreshed;
    }
    activeGameId.clear();
    activeGameTitle.clear();
    activeGamePlatform = game::GamePlatform::Unknown;
    sessionAccumulatedMs = 0;
  };
  auto startGame = [&](const library::GameEntry &gameEntry) {
    if (gameEntry.filePath.empty()) {
      showToast(i18n::TextId::ChooseRom);
      return false;
    }
    if (!runtimeRegistry.supports(gameEntry.platform)) {
      const std::string message =
          "No installed runtime supports this game format";
      std::fprintf(stderr, "%s\n", message.c_str());
      toastMessage = message;
      toastUntil = SDL_GetTicks64() + 3500;
      return false;
    }
    std::unique_ptr<runtime::IGameRuntime> nextRuntime =
        runtimeRegistry.create(gameEntry.platform);
    if (nextRuntime)
      nextRuntime->setInputBindings(settings.input);
    if (!nextRuntime ||
        !nextRuntime->load(gameEntry, renderer, settings.volume)) {
      const std::string message =
          nextRuntime ? std::string(nextRuntime->error())
                      : "No installed runtime supports this platform";
      std::fprintf(stderr, "%s\n", message.c_str());
      toastMessage = message;
      toastUntil = SDL_GetTicks64() + 3500;
      return false;
    }
    activeRuntime = std::move(nextRuntime);
    activeGameId = gameEntry.id;
    activeGameTitle = gameEntry.title;
    activeGamePlatform = gameEntry.platform;
    refreshActiveVideo();
    syncVideoOptions();
    syncAudioOptions();
    syncPlaybackOptions();
    syncPlayerControllers();
    sessionAccumulatedMs = 0;
    sessionActiveSince = SDL_GetTicks64();
    screen = Screen::Game;
    SDL_SetWindowTitle(
        window, (std::string("MRG Player - ") + gameEntry.title).c_str());
    std::fprintf(stdout, "Loaded %s with %s\n", gameEntry.filePath.c_str(),
                 std::string(activeRuntime->name()).c_str());
    return true;
  };
  auto startSelected = [&] {
    if (selectedGame < 0 ||
        selectedGame >= static_cast<int>(gameLibrary.entries().size())) {
      showToast(i18n::TextId::ChooseRom);
      return false;
    }
    return startGame(
        gameLibrary.entries()[static_cast<std::size_t>(selectedGame)]);
  };
  auto importGame = [&](const std::string &path, bool startAfterImport) {
    const library::ImportStatus status = gameLibrary.importGame(path);
    if (status == library::ImportStatus::Failed) {
      std::fprintf(stderr, "Game import failed: %s\n",
                   std::string(gameLibrary.error()).c_str());
      showToast(i18n::TextId::ImportFailed);
      return false;
    }
    if (gameLibrary.lastImportedIndex() >= 0)
      selectedGame = gameLibrary.lastImportedIndex();
    showToast(status == library::ImportStatus::Imported
                  ? i18n::TextId::ImportSuccess
                  : i18n::TextId::AlreadyImported);
    if (startAfterImport && selectedGame >= 0 &&
        selectedGame < static_cast<int>(gameLibrary.entries().size()))
      return startGame(
          gameLibrary.entries()[static_cast<std::size_t>(selectedGame)]);
    return true;
  };
  auto chooseAndImportGame = [&] {
    std::string pickerError;
    const std::optional<std::string> path = platform::pickGameFile(pickerError);
    if (path.has_value()) {
      (void)importGame(*path, false);
    } else if (!pickerError.empty()) {
      std::fprintf(stderr, "%s\n", pickerError.c_str());
      showToast(i18n::TextId::PickerUnavailable, 4000);
    }
  };
  auto leaveGame = [&] {
    recordCurrentSession();
    rewindHeld = false;
    if (activeRuntime) {
      activeRuntime->setFastForward(false);
      activeRuntime->setSlowMotion(false);
      activeRuntime->stop();
    }
    activeRuntime.reset();
    clearStateSlots();
    screen = Screen::Home;
    SDL_SetWindowTitle(window, "MRG Player");
  };
  auto resumeGame = [&] {
    if (!activeRuntime)
      return;
    activeRuntime->setPaused(false);
    resumeSessionClock();
    pauseFeedback.clear();
    screen = Screen::Game;
  };
  auto enterPause = [&] {
    if (!activeRuntime)
      return;
    activeRuntime->setPaused(true);
    activeRuntime->setFastForward(false);
    activeRuntime->setSlowMotion(false);
    rewindHeld = false;
    pauseSessionClock();
    pauseAction = 0;
    pauseConfirmingPower = false;
    pausePowerChoice = 0;
    pauseVideoPanel = false;
    pauseVideoRow = 0;
    pauseCoreOptionsPanel = false;
    pauseCoreOptionCursor = 0;
    pauseCoreOptionScroll = 0;
    pauseFeedback.clear();
    refreshStateSlots();
    screen = Screen::Pause;
  };
  auto showPauseFeedback = [&](i18n::TextId id) {
    pauseFeedback = i18n::tr(id, currentLanguage());
    pauseFeedbackUntil = SDL_GetTicks64() + 2600;
  };
  auto showBanner = [&](const std::string &message, Uint64 duration = 1600) {
    bannerMessage = message;
    bannerUntil = SDL_GetTicks64() + duration;
  };
  // Choosing a platform never downloads anything. The confirm key installs a
  // missing core and shows the details of an installed one, so a stray press on
  // the page cannot cost a multi-megabyte download it already has.
  auto activateCoreRow = [&](int row) {
    if (coreInstallBusy || row < 0 ||
        row >= static_cast<int>(coreRows.size())) {
      return;
    }
    const ui::CoreRow &entry = coreRows[static_cast<std::size_t>(row)];
    if (!entry.installed) {
      startCoreInstall(row);
      return;
    }
    std::string details = entry.platform + "  " + entry.core;
    if (!entry.size.empty())
      details += "  " + entry.size;
    showBanner(details, 3200);
  };
  // The frame is grabbed after the scene is drawn and before it is presented,
  // so the file always matches what the player is looking at.
  auto saveScreenshot = [&]() -> std::string {
    std::string error;
    const std::string path = saveFrameToFile(renderer, error);
    if (path.empty()) {
      showBanner(i18n::tr(i18n::TextId::ScreenshotFailed, currentLanguage()));
      if (!error.empty())
        std::fprintf(stderr, "screenshot failed: %s\n", error.c_str());
      return {};
    }
    showBanner(i18n::tr(i18n::TextId::ScreenshotSaved, currentLanguage()));
    std::printf("screenshot: %s\n", path.c_str());
    return path;
  };
  auto toggleMute = [&] {
    settings.audioMuted = !settings.audioMuted;
    saveSettings(settings);
    syncAudioOptions();
    showBanner(
        std::string(i18n::tr(i18n::TextId::Mute, currentLanguage())) + ": " +
        i18n::tr(settings.audioMuted ? i18n::TextId::On : i18n::TextId::Off,
                 currentLanguage()));
  };
  auto pauseVideoOptions = [&] {
    runtime::VideoOptions video;
    video.aspect =
        static_cast<runtime::AspectMode>(std::clamp(pauseVideo.aspect, 0, 2));
    video.integerScale = pauseVideo.integerScale;
    video.overscan = std::clamp(pauseVideo.overscan, 0, runtime::kMaxOverscan);
    return video;
  };
  auto openPauseVideoPanel = [&] {
    pauseVideoPanel = true;
    pauseVideoRow = 0;
    pauseVideo.aspect = static_cast<int>(activeVideo.aspect);
    pauseVideo.integerScale = activeVideo.integerScale;
    pauseVideo.overscan = activeVideo.overscan;
    runtime::VideoOptions probe;
    pauseVideo.overrideEnabled =
        !activeGameId.empty() &&
        gameLibrary.videoOverride(activeGameId, activeGamePlatform, probe);
  };
  // Edits preview on the running picture; leaving the panel without an
  // override restores whatever the game used before.
  auto closePauseVideoPanel = [&] {
    pauseVideoPanel = false;
    if (!pauseVideo.overrideEnabled) {
      activeVideo = gameVideoOptions();
      syncVideoOptions();
    }
  };
  auto commitPauseVideo = [&] {
    if (activeGameId.empty() || !pauseVideo.overrideEnabled)
      return;
    if (!gameLibrary.setVideoOverride(activeGameId, activeGamePlatform,
                                      pauseVideoOptions())) {
      std::fprintf(stderr, "Display override failed: %s\n",
                   std::string(gameLibrary.error()).c_str());
    }
  };
  auto togglePauseVideoOverride = [&] {
    if (pauseVideo.overrideEnabled) {
      pauseVideo.overrideEnabled = false;
      if (!activeGameId.empty())
        (void)gameLibrary.clearVideoOverride(activeGameId, activeGamePlatform);
      activeVideo = globalVideoOptions();
      pauseVideo.aspect = static_cast<int>(activeVideo.aspect);
      pauseVideo.integerScale = activeVideo.integerScale;
      pauseVideo.overscan = activeVideo.overscan;
      syncVideoOptions();
      return;
    }
    pauseVideo.overrideEnabled = true;
    commitPauseVideo();
    activeVideo = pauseVideoOptions();
    syncVideoOptions();
  };
  auto adjustPauseVideo = [&](int row, int direction) {
    if (row == 0) {
      pauseVideo.aspect = (pauseVideo.aspect + direction + 3) % 3;
    } else if (row == 1) {
      pauseVideo.integerScale = !pauseVideo.integerScale;
    } else if (row == 2) {
      pauseVideo.overscan = std::clamp(pauseVideo.overscan + direction * 4, 0,
                                       runtime::kMaxOverscan);
    } else {
      togglePauseVideoOverride();
      return;
    }
    activeVideo = pauseVideoOptions();
    syncVideoOptions();
    commitPauseVideo();
  };
  auto activatePauseVideoRow = [&] {
    if (pauseVideoRow + 1 == ui::kPauseVideoRowCount)
      togglePauseVideoOverride();
    else
      adjustPauseVideo(pauseVideoRow, 1);
  };
  auto coreOptionList = [&]() -> const runtime::CoreOptionList & {
    static const runtime::CoreOptionList empty;
    return activeRuntime ? activeRuntime->coreOptions() : empty;
  };
  auto clampCoreOptionScroll = [&] {
    const int total = static_cast<int>(coreOptionList().size());
    const int maximum = std::max(0, total - ui::kPauseCoreOptionRows);
    pauseCoreOptionScroll = std::clamp(pauseCoreOptionScroll, 0, maximum);
  };
  auto openPauseCoreOptionsPanel = [&] {
    pauseCoreOptionsPanel = true;
    pauseCoreOptionCursor = 0;
    pauseCoreOptionScroll = 0;
  };
  auto closePauseCoreOptionsPanel = [&] { pauseCoreOptionsPanel = false; };
  auto movePauseCoreOption = [&](int direction) {
    const auto &options = coreOptionList();
    if (options.empty())
      return;
    const int total = static_cast<int>(options.size());
    pauseCoreOptionCursor = (pauseCoreOptionCursor + direction + total) % total;
    if (pauseCoreOptionCursor < pauseCoreOptionScroll) {
      pauseCoreOptionScroll = pauseCoreOptionCursor;
    } else if (pauseCoreOptionCursor >=
               pauseCoreOptionScroll + ui::kPauseCoreOptionRows) {
      pauseCoreOptionScroll =
          pauseCoreOptionCursor - ui::kPauseCoreOptionRows + 1;
    }
    clampCoreOptionScroll();
  };
  // Changing a value tells the core through the environment callback; the
  // runtime persists it and flags the next GET_VARIABLE_UPDATE.
  auto changePauseCoreOption = [&](int direction) {
    if (!activeRuntime)
      return;
    const auto &options = activeRuntime->coreOptions();
    if (options.empty())
      return;
    const int index = std::clamp(pauseCoreOptionCursor, 0,
                                 static_cast<int>(options.size()) - 1);
    const runtime::CoreOption &option =
        options[static_cast<std::size_t>(index)];
    if (option.values.empty())
      return;
    const int count = static_cast<int>(option.values.size());
    const int selected =
        (static_cast<int>(option.selected) + direction + count) % count;
    if (!activeRuntime->setCoreOption(
            option.key,
            option.values[static_cast<std::size_t>(selected)].value)) {
      std::fprintf(stderr, "Core option %s refused a new value\n",
                   option.key.c_str());
    }
  };
  auto resetPauseCoreOptions = [&] {
    if (activeRuntime)
      activeRuntime->resetCoreOptions();
  };
  auto executePauseAction = [&] {
    if (!activeRuntime)
      return;
    if (pauseAction == 0) {
      resumeGame();
      return;
    }
    if (pauseAction == 3) {
      activeRuntime->setFastForward(false);
      activeRuntime->reset();
      resumeGame();
      return;
    }
    if (pauseAction == 4) {
      pauseConfirmingPower = true;
      pausePowerChoice = 0;
      return;
    }
    if (pauseAction == 5) {
      openPauseVideoPanel();
      return;
    }
    if (pauseAction == 6) {
      openPauseCoreOptionsPanel();
      return;
    }
    if (pauseAction == 7) {
      leaveGame();
      return;
    }
    std::string stateError;
    const runtime::StateStatus status =
        pauseAction == 1 ? activeRuntime->saveState(stateSlot, stateError)
                         : activeRuntime->loadState(stateSlot, stateError);
    if (status == runtime::StateStatus::Saved) {
      refreshStateSlots();
      showPauseFeedback(i18n::TextId::StateSaved);
    } else if (status == runtime::StateStatus::Loaded)
      showPauseFeedback(i18n::TextId::StateLoaded);
    else if (status == runtime::StateStatus::Missing)
      showPauseFeedback(i18n::TextId::StateMissing);
    else {
      if (!stateError.empty())
        std::fprintf(stderr, "Save-state operation failed: %s\n",
                     stateError.c_str());
      showPauseFeedback(i18n::TextId::StateFailed);
    }
  };
  auto confirmPausePower = [&] {
    const bool confirmed = pausePowerChoice == 1;
    pauseConfirmingPower = false;
    if (!activeRuntime || !confirmed)
      return;
    activeRuntime->powerCycle();
    if (!activeRuntime->error().empty()) {
      std::fprintf(stderr, "Power cycle failed: %s\n",
                   std::string(activeRuntime->error()).c_str());
      leaveGame();
      showToast(i18n::TextId::StateFailed);
      return;
    }
    resumeGame();
  };
  auto selectedEntryAvailable = [&] {
    return selectedGame >= 0 &&
           selectedGame < static_cast<int>(gameLibrary.entries().size());
  };
  auto refreshSaveStatus = [&] {
    saveDataPresent = false;
    if (!selectedEntryAvailable())
      return;
    std::string saveError;
    const runtime::SaveDataStatus status = runtimeRegistry.inspectSaveData(
        gameLibrary.entries()[static_cast<std::size_t>(selectedGame)],
        saveError);
    saveDataPresent = status == runtime::SaveDataStatus::Present;
    if (status == runtime::SaveDataStatus::Failed)
      std::fprintf(stderr, "Save data inspection failed: %s\n",
                   saveError.c_str());
  };
  auto openManagement = [&](ui::ManagementDialog initialDialog) {
    if (!selectedEntryAvailable())
      return false;
    managementAction = 0;
    confirmationChoice = 0;
    managementDialog = initialDialog;
    refreshSaveStatus();
    screen = Screen::Management;
    return true;
  };
  // Runs the footer entry the direction keys are pointing at, so the same
  // action is reachable from the row and from its button.
  auto activateHomeAction = [&] {
    // An empty library has no footer entry, so there is nothing to run; quitting
    // stays on the Escape key and the top-right control.
    if (!selectedEntryAvailable())
      return;
    if (homeAction == 0)
      (void)startSelected();
    else if (homeAction == 1)
      (void)openManagement(ui::ManagementDialog::Menu);
    else
      running = false;
  };
  auto executeManagementAction = [&] {
    switch (managementAction) {
    case 0:
      (void)startSelected();
      break;
    case 4:
      confirmationChoice = 0;
      managementDialog = ui::ManagementDialog::ConfirmRemoveRom;
      break;
    case 5:
      if (!saveDataPresent) {
        showToast(i18n::TextId::NoSaveData);
      } else {
        confirmationChoice = 0;
        managementDialog = ui::ManagementDialog::ConfirmDeleteSave;
      }
      break;
    case 1: {
      if (!selectedEntryAvailable())
        break;
      const library::GameEntry entry =
          gameLibrary.entries()[static_cast<std::size_t>(selectedGame)];
      if (!gameLibrary.setFavorite(static_cast<std::size_t>(selectedGame),
                                   !entry.favorite)) {
        std::fprintf(stderr, "Favorite update failed: %s\n",
                     std::string(gameLibrary.error()).c_str());
        break;
      }
      selectedGame = gameLibrary.indexOfId(entry.id, entry.platform);
      if (selectedGame < 0) {
        screen = Screen::Home;
        break;
      }
      showToast(entry.favorite ? i18n::TextId::Unfavorite
                               : i18n::TextId::Favorite);
      break;
    }
    case 2:
      managementDialog = ui::ManagementDialog::Info;
      break;
    default:
      screen = Screen::Home;
      break;
    }
  };
  auto confirmManagementAction = [&] {
    if (confirmationChoice == 0) {
      managementDialog = ui::ManagementDialog::Menu;
      return;
    }
    if (!selectedEntryAvailable()) {
      screen = Screen::Home;
      return;
    }
    if (managementDialog == ui::ManagementDialog::ConfirmRemoveRom) {
      const int removedIndex = selectedGame;
      if (gameLibrary.removeGame(static_cast<std::size_t>(selectedGame))) {
        selectedGame = std::clamp(
            removedIndex, 0, static_cast<int>(gameLibrary.slotCount()) - 1);
        screen = Screen::Home;
        showToast(i18n::TextId::RomRemoved);
      } else {
        std::fprintf(stderr, "ROM removal failed: %s\n",
                     std::string(gameLibrary.error()).c_str());
        managementDialog = ui::ManagementDialog::Menu;
        showToast(i18n::TextId::RomRemoveFailed);
      }
      return;
    }
    if (managementDialog == ui::ManagementDialog::ConfirmDeleteSave) {
      std::string saveError;
      const runtime::SaveDataStatus status = runtimeRegistry.deleteSaveData(
          gameLibrary.entries()[static_cast<std::size_t>(selectedGame)],
          saveError);
      managementDialog = ui::ManagementDialog::Menu;
      refreshSaveStatus();
      if (status == runtime::SaveDataStatus::Removed) {
        showToast(i18n::TextId::SaveDeleted);
      } else if (status == runtime::SaveDataStatus::Missing) {
        showToast(i18n::TextId::NoSaveData);
      } else {
        std::fprintf(stderr, "Save data deletion failed: %s\n",
                     saveError.c_str());
        showToast(i18n::TextId::SaveDeleteFailed);
      }
    }
  };

#if defined(MRG_ENABLE_SMOKE_TESTS)
  if (options.smokeManagement) {
    if (options.romPath.empty()) {
      std::fprintf(stderr, "--smoke-test-management requires a ROM path\n");
      startupRomFailed = true;
      running = false;
    } else if (!importGame(options.romPath, false) ||
               !openManagement(ui::ManagementDialog::Menu)) {
      startupRomFailed = true;
      running = false;
    }
  } else if (!options.romPath.empty()) {
    const library::GameEntry smokeGame{
        paths::toUtf8(paths::fromUtf8(options.romPath).stem()), "Smoke Test",
        options.romPath, game::platformFromExtension(options.romPath)};
    startupRomFailed = !startGame(smokeGame);
    if (!startupRomFailed && options.smokePause && activeRuntime) {
      for (int frame = 0; frame < 4; ++frame) {
        SDL_Delay(18);
        activeRuntime->update();
      }
      std::string stateError;
      const runtime::StateStatus saved =
          activeRuntime->saveState(0, stateError);
      const runtime::StateStatus loaded =
          activeRuntime->loadState(0, stateError);
      if (saved != runtime::StateStatus::Saved ||
          loaded != runtime::StateStatus::Loaded) {
        std::fprintf(stderr, "Save-state smoke failed: %s\n",
                     stateError.c_str());
        startupRomFailed = true;
        running = false;
      } else {
        // Exercise fast-forward, the reset button and a cold power cycle so a
        // broken path fails the smoke run instead of shipping silently.
        activeRuntime->setFastForward(true);
        // Run part of the fast-forward stretch with the audible policy so both
        // branches of the audio handling are exercised.
        activeRuntime->setFastForwardAudio(true);
        for (int frame = 0; frame < 4; ++frame) {
          SDL_Delay(6);
          activeRuntime->update();
        }
        activeRuntime->setFastForwardAudio(false);
        activeRuntime->setFastForward(false);
        activeRuntime->reset();
        activeRuntime->powerCycle();
        for (int frame = 0; frame < 2; ++frame) {
          SDL_Delay(18);
          activeRuntime->update();
        }
        if (!activeRuntime->error().empty()) {
          std::fprintf(stderr, "Reset/power-cycle smoke failed: %s\n",
                       std::string(activeRuntime->error()).c_str());
          startupRomFailed = true;
          running = false;
        } else {
          // Pause features: one-frame stepping, slow motion, and the slot
          // metadata the pause card reads back.
          activeRuntime->setSlowMotion(true);
          activeRuntime->update();
          activeRuntime->setSlowMotion(false);
          activeRuntime->stepFrame();
          activeRuntime->stepFrame();
          // Mute hotkey: flip it on and straight back off so the saved setting
          // survives, while the banner below still proves the path ran.
          toggleMute();
          toggleMute();
          enterPause();
          const runtime::StateInfo info = activeRuntime->stateInfo(0);
          if (!info.present || info.thumbnailPath.empty()) {
            std::fprintf(stderr, "State metadata smoke failed\n");
            startupRomFailed = true;
            running = false;
          } else {
            showPauseFeedback(i18n::TextId::StateLoaded);
          }
        }
      }
    }
    if (startupRomFailed && (options.smokeRom || options.smokePause))
      running = false;
  } else if (options.smokeRom || options.smokePause) {
    std::fprintf(stderr, "%s requires a ROM path\n",
                 options.smokePause ? "--smoke-test-pause"
                                    : "--smoke-test-rom");
    startupRomFailed = true;
    running = false;
  }
#endif

  // The launcher can start on either screen, so the installer page is filled
  // once here as well.
  refreshCoreRows();

  while (running) {
    if (coreInstallDone.exchange(false)) {
      if (coreInstallThread != nullptr) {
        SDL_WaitThread(coreInstallThread, nullptr);
        coreInstallThread = nullptr;
      }
      coreInstallBusy = false;
      if (!coreInstallOk)
        std::fprintf(stderr, "core install: %s\n", coreInstallMessage.c_str());
      showBanner(i18n::tr(coreInstallOk ? i18n::TextId::CoreInstalledBanner
                                        : i18n::TextId::CoreInstallFailedBanner,
                          currentLanguage()),
                 3000);
      refreshCoreRows();
    }
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ||
          event.type == SDL_MOUSEBUTTONDOWN ||
          event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEWHEEL ||
          event.type == SDL_MOUSEMOTION) {
        lastInputDevice = config::InputDeviceKind::KeyboardMouse;
      } else if (event.type == SDL_CONTROLLERBUTTONDOWN ||
                 event.type == SDL_CONTROLLERBUTTONUP ||
                 (event.type == SDL_CONTROLLERAXISMOTION &&
                  std::abs(event.caxis.value) >
                      settings.input.analogDeadZone)) {
        lastInputDevice = config::InputDeviceKind::Controller;
      }

      if (event.type == SDL_QUIT) {
        if (quitConfirm) {
          running = false;
        } else {
          quitConfirm = true;
          quitChoice = 0;
        }
        continue;
      }
      if (event.type == SDL_DROPFILE) {
        const std::string dropped =
            event.drop.file != nullptr ? event.drop.file : "";
        SDL_free(event.drop.file);
        // File drops are a home-only action; never import behind a modal or
        // replace a running/paused game. The SDL payload is freed in all cases.
        if (screen == Screen::Home && !quitConfirm)
          (void)importGame(dropped, true);
        continue;
      }
      if (event.type == SDL_CONTROLLERDEVICEADDED) {
        if (SDL_IsGameController(event.cdevice.which)) {
          const std::string addedGuid = controllerGuidAt(event.cdevice.which);
          const bool preferredReturned =
              !settings.preferredControllerGuid.empty() &&
              addedGuid == settings.preferredControllerGuid &&
              openedControllerGuid(controller) != addedGuid;
          if (controller == nullptr || preferredReturned) {
            if (controller != nullptr)
              SDL_GameControllerClose(controller);
            controller = SDL_GameControllerOpen(event.cdevice.which);
          } else if (controller2 == nullptr) {
            controller2 = SDL_GameControllerOpen(event.cdevice.which);
            if (controller2 != nullptr)
              settings.preferredControllerGuid2 =
                  openedControllerGuid(controller2);
          }
          syncPlayerControllers();
        }
        continue;
      }
      if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        if (controller != nullptr &&
            instanceIdOf(controller) == event.cdevice.which) {
          SDL_GameControllerClose(controller);
          controller = nullptr;
          if (activeRuntime)
            activeRuntime->releaseInput();
          controller =
              openPreferredController(settings.preferredControllerGuid);
        } else if (controller2 != nullptr &&
                   instanceIdOf(controller2) == event.cdevice.which) {
          SDL_GameControllerClose(controller2);
          controller2 = nullptr;
          if (activeRuntime)
            activeRuntime->releaseInput();
          openSecondController();
        }
        syncPlayerControllers();
        continue;
      }

      if (event.type == SDL_WINDOWEVENT &&
          event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        if (activeRuntime)
          activeRuntime->releaseInput();
      }

      // The quit dialog is modal: it takes every event from every device, and
      // nothing below this point sees one. Keeping it here, ahead of the game
      // and pause branches, is what stops a click from answering the dialog and
      // then acting on the screen behind it.
      if (quitConfirm) {
        // The dialog owns the presses, but releases still belong to the game:
        // a button held when the dialog opened must not stay pressed after it
        // closes, and the game keeps running underneath the dialog.
        if (activeRuntime && (event.type == SDL_KEYUP ||
                              event.type == SDL_CONTROLLERBUTTONUP ||
                              event.type == SDL_CONTROLLERAXISMOTION)) {
          activeRuntime->handleEvent(event);
        }
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
          const SDL_Keycode key = event.key.keysym.sym;
          if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP ||
              key == SDLK_DOWN) {
            quitChoice = 1 - quitChoice;
          } else if (key == SDLK_RETURN || key == SDLK_SPACE) {
            if (quitChoice == 1)
              running = false;
            else
              quitConfirm = false;
          } else if (key == SDLK_ESCAPE || key == SDLK_b) {
            quitConfirm = false;
          }
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
          if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
              event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ||
              event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
              event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            quitChoice = 1 - quitChoice;
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            if (quitChoice == 1)
              running = false;
            else
              quitConfirm = false;
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
                     event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
            quitConfirm = false;
          }
        } else if (event.type == SDL_MOUSEMOTION ||
                   event.type == SDL_MOUSEBUTTONDOWN) {
          const bool motion = event.type == SDL_MOUSEMOTION;
          float x = 0.0f;
          float y = 0.0f;
          ui::windowToCanvas(
              motion ? static_cast<float>(event.motion.x)
                     : static_cast<float>(event.button.x),
              motion ? static_cast<float>(event.motion.y)
                     : static_cast<float>(event.button.y),
              x, y);
          const int choice = ui::quitConfirmChoiceAt(x, y);
          if (motion) {
            if (choice >= 0)
              quitChoice = choice;
          } else if (event.button.button == SDL_BUTTON_RIGHT) {
            quitConfirm = false;
          } else if (event.button.button == SDL_BUTTON_LEFT && choice >= 0) {
            quitChoice = choice;
            if (choice == 1)
              running = false;
            else
              quitConfirm = false;
          }
        }
        continue;
      }

      if (screen == Screen::Settings && bindingCapture) {
        const std::size_t player =
            static_cast<std::size_t>(std::clamp(settings.bindingsPlayer, 0, 1));
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
          if (event.key.keysym.sym != SDLK_ESCAPE) {
            const std::size_t index = static_cast<std::size_t>(settingRow - 4);
            config::keyboardBindings(settings.input, player)[index] =
                event.key.keysym.sym;
            saveSettings(settings);
            if (activeRuntime)
              activeRuntime->setInputBindings(settings.input);
          }
          bindingCapture = false;
          continue;
        }
        if (event.type == SDL_CONTROLLERBUTTONDOWN) {
          const std::size_t index = static_cast<std::size_t>(settingRow - 4);
          config::controllerBindings(settings.input, player)[index] =
              static_cast<SDL_GameControllerButton>(event.cbutton.button);
          saveSettings(settings);
          if (activeRuntime)
            activeRuntime->setInputBindings(settings.input);
          bindingCapture = false;
          continue;
        }
      }

      if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
          event.key.keysym.sym == SDLK_F11) {
        settings.fullscreen = !settings.fullscreen;
        SDL_SetWindowFullscreen(
            window, settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        saveSettings(settings);
        continue;
      }

      // F12 saves a screenshot from both the game and the pause screen.
      if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
          event.key.keysym.sym == SDLK_F12 &&
          (screen == Screen::Game || screen == Screen::Pause)) {
        screenshotRequested = true;
        continue;
      }

      // Mute works wherever the picture is running, so a quick silence does not
      // require opening the pause menu.
      if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
          event.key.keysym.sym == SDLK_m &&
          (screen == Screen::Game || screen == Screen::Pause)) {
        toggleMute();
        continue;
      }

      if (screen == Screen::Game) {
        if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
            event.key.keysym.sym == SDLK_TAB) {
          if (event.type == SDL_KEYDOWN && event.key.repeat != 0)
            continue;
          if (activeRuntime)
            activeRuntime->setFastForward(event.type == SDL_KEYDOWN);
          continue;
        }
        if ((event.type == SDL_CONTROLLERBUTTONDOWN ||
             event.type == SDL_CONTROLLERBUTTONUP) &&
            event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
          if (activeRuntime)
            activeRuntime->setFastForward(event.type ==
                                          SDL_CONTROLLERBUTTONDOWN);
          continue;
        }
        if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
            event.key.keysym.sym == SDLK_LSHIFT) {
          if (event.type == SDL_KEYDOWN && event.key.repeat != 0)
            continue;
          if (activeRuntime)
            activeRuntime->setSlowMotion(event.type == SDL_KEYDOWN);
          continue;
        }
        if ((event.type == SDL_CONTROLLERBUTTONDOWN ||
             event.type == SDL_CONTROLLERBUTTONUP) &&
            event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
          if (activeRuntime)
            activeRuntime->setSlowMotion(event.type ==
                                         SDL_CONTROLLERBUTTONDOWN);
          continue;
        }
        if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
            event.key.keysym.sym == SDLK_BACKSPACE) {
          if (event.type == SDL_KEYDOWN && event.key.repeat != 0)
            continue;
          rewindHeld = event.type == SDL_KEYDOWN;
          continue;
        }
        if ((event.type == SDL_CONTROLLERBUTTONDOWN ||
             event.type == SDL_CONTROLLERBUTTONUP) &&
            event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK) {
          rewindHeld = event.type == SDL_CONTROLLERBUTTONDOWN;
          continue;
        }
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
            event.key.keysym.sym == SDLK_ESCAPE) {
          enterPause();
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                   (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE ||
                    event.cbutton.button == SDL_CONTROLLER_BUTTON_Y)) {
          enterPause();
        } else if (activeRuntime) {
          activeRuntime->handleEvent(event);
        }
        continue;
      }

      if (screen == Screen::Pause && pauseConfirmingPower) {
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
          const SDL_Keycode key = event.key.keysym.sym;
          if (key == SDLK_ESCAPE || key == SDLK_b)
            pauseConfirmingPower = false;
          else if (key == SDLK_LEFT || key == SDLK_RIGHT ||
                   key == SDLK_UP || key == SDLK_DOWN)
            pausePowerChoice = 1 - pausePowerChoice;
          else if (key == SDLK_RETURN || key == SDLK_SPACE)
            confirmPausePower();
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
          const Uint8 button = event.cbutton.button;
          if (button == SDL_CONTROLLER_BUTTON_B ||
              button == SDL_CONTROLLER_BUTTON_BACK)
            pauseConfirmingPower = false;
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
                   button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ||
                   button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
                   button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            pausePowerChoice = 1 - pausePowerChoice;
          else if (button == SDL_CONTROLLER_BUTTON_A)
            confirmPausePower();
        } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                   event.button.button == SDL_BUTTON_LEFT) {
          float x = 0.0f;
          float y = 0.0f;
          ui::windowToCanvas(event.button.x, event.button.y, x, y);
          const int choice = ui::powerCycleChoiceAt(x, y);
          if (choice >= 0) {
            pausePowerChoice = choice;
            confirmPausePower();
          }
        }
        continue;
      }

      if (screen == Screen::Pause && pauseCoreOptionsPanel) {
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
          const SDL_Keycode key = event.key.keysym.sym;
          if (key == SDLK_ESCAPE || key == SDLK_b)
            closePauseCoreOptionsPanel();
          else if (key == SDLK_UP)
            movePauseCoreOption(-1);
          else if (key == SDLK_DOWN)
            movePauseCoreOption(1);
          else if (key == SDLK_LEFT)
            changePauseCoreOption(-1);
          else if (key == SDLK_RIGHT)
            changePauseCoreOption(1);
          else if (key == SDLK_r)
            resetPauseCoreOptions();
          else if (key == SDLK_RETURN || key == SDLK_SPACE)
            changePauseCoreOption(1);
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
          const Uint8 button = event.cbutton.button;
          if (button == SDL_CONTROLLER_BUTTON_B ||
              button == SDL_CONTROLLER_BUTTON_BACK)
            closePauseCoreOptionsPanel();
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_UP)
            movePauseCoreOption(-1);
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            movePauseCoreOption(1);
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT)
            changePauseCoreOption(-1);
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
            changePauseCoreOption(1);
          else if (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
            resetPauseCoreOptions();
          else if (button == SDL_CONTROLLER_BUTTON_A)
            changePauseCoreOption(1);
        } else if (event.type == SDL_MOUSEBUTTONDOWN) {
          float x = 0.0f;
          float y = 0.0f;
          ui::windowToCanvas(event.button.x, event.button.y, x, y);
          if (event.button.button == SDL_BUTTON_RIGHT) {
            closePauseCoreOptionsPanel();
          } else if (event.button.button == SDL_BUTTON_LEFT) {
            const int row = ui::pauseCoreOptionAt(x, y, pauseCoreOptionScroll);
            const int total = static_cast<int>(coreOptionList().size());
            if (row >= 0 && row < total) {
              pauseCoreOptionCursor = row;
              changePauseCoreOption(1);
            }
          }
        } else if (event.type == SDL_MOUSEWHEEL && event.wheel.y != 0) {
          movePauseCoreOption(event.wheel.y < 0 ? 1 : -1);
        }
        continue;
      }

      if (screen == Screen::Pause && pauseVideoPanel) {
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
          const SDL_Keycode key = event.key.keysym.sym;
          if (key == SDLK_ESCAPE || key == SDLK_b)
            closePauseVideoPanel();
          else if (key == SDLK_UP)
            pauseVideoRow = (pauseVideoRow - 1 + ui::kPauseVideoRowCount) %
                            ui::kPauseVideoRowCount;
          else if (key == SDLK_DOWN)
            pauseVideoRow = (pauseVideoRow + 1) % ui::kPauseVideoRowCount;
          else if (key == SDLK_LEFT)
            adjustPauseVideo(pauseVideoRow, -1);
          else if (key == SDLK_RIGHT)
            adjustPauseVideo(pauseVideoRow, 1);
          else if (key == SDLK_RETURN || key == SDLK_SPACE)
            activatePauseVideoRow();
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
          const Uint8 button = event.cbutton.button;
          if (button == SDL_CONTROLLER_BUTTON_B ||
              button == SDL_CONTROLLER_BUTTON_BACK)
            closePauseVideoPanel();
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_UP)
            pauseVideoRow = (pauseVideoRow - 1 + ui::kPauseVideoRowCount) %
                            ui::kPauseVideoRowCount;
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            pauseVideoRow = (pauseVideoRow + 1) % ui::kPauseVideoRowCount;
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT)
            adjustPauseVideo(pauseVideoRow, -1);
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
            adjustPauseVideo(pauseVideoRow, 1);
          else if (button == SDL_CONTROLLER_BUTTON_A)
            activatePauseVideoRow();
        } else if (event.type == SDL_MOUSEBUTTONDOWN) {
          float x = 0.0f;
          float y = 0.0f;
          ui::windowToCanvas(event.button.x, event.button.y, x, y);
          if (event.button.button == SDL_BUTTON_RIGHT) {
            closePauseVideoPanel();
          } else if (event.button.button == SDL_BUTTON_LEFT) {
            const int row = ui::pauseVideoRowAt(x, y);
            if (row >= 0) {
              pauseVideoRow = row;
              activatePauseVideoRow();
            }
          }
        }
        continue;
      }

      if (screen == Screen::Pause) {
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
          const SDL_Keycode key = event.key.keysym.sym;
          if (key == SDLK_ESCAPE || key == SDLK_b)
            resumeGame();
          else if (key == SDLK_UP)
            pauseAction = (pauseAction - 1 + ui::kPauseActionCount) %
                          ui::kPauseActionCount;
          else if (key == SDLK_DOWN)
            pauseAction = (pauseAction + 1) % ui::kPauseActionCount;
          else if (key == SDLK_LEFT)
            stateSlot =
                (stateSlot - 1 + ui::kStateSlotCount) % ui::kStateSlotCount;
          else if (key == SDLK_RIGHT)
            stateSlot = (stateSlot + 1) % ui::kStateSlotCount;
          else if (key == SDLK_PERIOD) {
            if (activeRuntime)
              activeRuntime->stepFrame();
          } else if (key == SDLK_RETURN || key == SDLK_SPACE)
            executePauseAction();
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
          const Uint8 button = event.cbutton.button;
          if (button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            if (activeRuntime)
              activeRuntime->stepFrame();
            continue;
          }
          if (button == SDL_CONTROLLER_BUTTON_B ||
              button == SDL_CONTROLLER_BUTTON_BACK)
            resumeGame();
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_UP)
            pauseAction = (pauseAction - 1 + ui::kPauseActionCount) %
                          ui::kPauseActionCount;
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            pauseAction = (pauseAction + 1) % ui::kPauseActionCount;
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT)
            stateSlot =
                (stateSlot - 1 + ui::kStateSlotCount) % ui::kStateSlotCount;
          else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
            stateSlot = (stateSlot + 1) % ui::kStateSlotCount;
          else if (button == SDL_CONTROLLER_BUTTON_A)
            executePauseAction();
        } else if (event.type == SDL_CONTROLLERAXISMOTION &&
                   SDL_GetTicks64() - lastAxisMove > 240) {
          const int direction = event.caxis.value < -22000
                                    ? -1
                                    : (event.caxis.value > 22000 ? 1 : 0);
          if (direction != 0 && event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
            stateSlot = (stateSlot + direction + ui::kStateSlotCount) %
                        ui::kStateSlotCount;
            lastAxisMove = SDL_GetTicks64();
          } else if (direction != 0 &&
                     event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            pauseAction = (pauseAction + direction + ui::kPauseActionCount) %
                          ui::kPauseActionCount;
            lastAxisMove = SDL_GetTicks64();
          }
        } else if (event.type == SDL_MOUSEBUTTONDOWN) {
          float x = 0.0f;
          float y = 0.0f;
          ui::windowToCanvas(event.button.x, event.button.y, x, y);
          if (event.button.button == SDL_BUTTON_RIGHT) {
            resumeGame();
          } else if (event.button.button == SDL_BUTTON_LEFT) {
            const int action = ui::pauseActionAt(x, y);
            if (action >= 0) {
              pauseAction = action;
              executePauseAction();
            } else {
              const int slot = ui::pauseSlotAt(x, y);
              if (slot >= 0 && slot < ui::kStateSlotCount)
                stateSlot = slot;
            }
          }
        } else if (event.type == SDL_MOUSEWHEEL && event.wheel.y != 0) {
          const int direction = event.wheel.y < 0 ? 1 : -1;
          pauseAction = (pauseAction + direction + ui::kPauseActionCount) %
                        ui::kPauseActionCount;
        }
        continue;
      }

      switch (event.type) {
      case SDL_KEYDOWN: {
        if (event.key.repeat != 0)
          break;
        const SDL_Keycode key = event.key.keysym.sym;
        if (screen == Screen::Home) {
          if (homeFocus == ui::HomeFocus::Actions &&
              (key == SDLK_LEFT || key == SDLK_RIGHT)) {
            const int count = ui::homeActionCount(selectedEntryAvailable());
            if (count > 0) {
              const int step = key == SDLK_LEFT ? count - 1 : 1;
              homeAction = (homeAction + step) % count;
            }
          } else if ((key == SDLK_RETURN || key == SDLK_SPACE) &&
                     homeFocus == ui::HomeFocus::Actions) {
            activateHomeAction();
          } else if (homeFocus == ui::HomeFocus::Pills &&
                     (key == SDLK_LEFT || key == SDLK_RIGHT)) {
            homePill = 1 - homePill;
          } else if ((key == SDLK_RETURN || key == SDLK_SPACE) &&
                     homeFocus == ui::HomeFocus::Pills) {
            if (homePill == 0)
              chooseAndImportGame();
            else
              openSettings();
          } else if (key == SDLK_DOWN && homeFocus == ui::HomeFocus::Pills) {
            homeFocus = ui::HomeFocus::Cards;
          } else if (key == SDLK_UP && homeFocus == ui::HomeFocus::Cards) {
            homeFocus = ui::HomeFocus::Pills;
          } else if (key == SDLK_DOWN && homeFocus == ui::HomeFocus::Cards &&
                     ui::homeActionCount(selectedEntryAvailable()) > 0) {
            homeFocus = ui::HomeFocus::Actions;
            homeAction = 0;
          } else if (key == SDLK_UP && homeFocus == ui::HomeFocus::Actions) {
            homeFocus = ui::HomeFocus::Cards;
          } else if (key == SDLK_LEFT)
            ui::moveSelection(selectedGame, -1,
                              static_cast<int>(gameLibrary.slotCount()));
          else if (key == SDLK_RIGHT)
            ui::moveSelection(selectedGame, 1,
                              static_cast<int>(gameLibrary.slotCount()));
          else if (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
            // The shoulders move the shelf by a screenful; the selection stops
            // at either end instead of wrapping around.
            const int count = static_cast<int>(gameLibrary.slotCount());
            const int direction = key == SDLK_PAGEDOWN ? 1 : -1;
            selectedGame =
                std::clamp(selectedGame + direction * 7, 0, count - 1);
          } else if (key == SDLK_m || key == SDLK_x)
            (void)openManagement(ui::ManagementDialog::Menu);
          else if (key == SDLK_DELETE)
            (void)openManagement(ui::ManagementDialog::ConfirmRemoveRom);
          else if (key == SDLK_RETURN || key == SDLK_SPACE)
            (void)startSelected();
          else if (key == SDLK_ESCAPE) {
            quitConfirm = true;
            quitChoice = 0;
          }
        } else if (screen == Screen::Settings) {
          const int rowCount = ui::settingRowCount(settingCategory);
          if (key == SDLK_ESCAPE || key == SDLK_b) {
            bindingCapture = false;
            if (settingsRailFocus)
              screen = Screen::Home;
            else
              settingsRailFocus = true;
          } else if (key == SDLK_r && settingCategory == 3 &&
                     !settingsRailFocus) {
            settings.input = config::defaultInputBindings();
            if (activeRuntime)
              activeRuntime->setInputBindings(settings.input);
            saveSettings(settings);
          } else if (key == SDLK_r && settingCategory == 5 &&
                     !settingsRailFocus) {
            refreshCoreRows();
          } else if (key == SDLK_LEFT) {
            settingsRailFocus = true;
          } else if (key == SDLK_RIGHT) {
            settingsRailFocus = false;
          } else if (key == SDLK_UP) {
            if (settingsRailFocus)
              changeCategory(-1);
            else
              settingRow = (settingRow - 1 + rowCount) % rowCount;
          } else if (key == SDLK_DOWN) {
            if (settingsRailFocus)
              changeCategory(1);
            else
              settingRow = (settingRow + 1) % rowCount;
          } else if (key == SDLK_RETURN || key == SDLK_SPACE) {
            if (settingsRailFocus) {
              settingsRailFocus = false;
            } else if (settingCategory == 5) {
              activateCoreRow(settingRow);
            } else if (settingCategory == 3 && settingRow >= 4 &&
                       settingRow <
                           4 + static_cast<int>(config::kInputActionCount)) {
              bindingCapture = true;
            } else {
              applySetting(1);
            }
          }
        } else if (managementDialog == ui::ManagementDialog::Menu) {
          if (key == SDLK_ESCAPE || key == SDLK_b)
            screen = Screen::Home;
          else if (key == SDLK_UP)
            managementAction =
                (managementAction - 1 + ui::kManagementActionCount) %
                ui::kManagementActionCount;
          else if (key == SDLK_DOWN)
            managementAction =
                (managementAction + 1) % ui::kManagementActionCount;
          else if (key == SDLK_RETURN || key == SDLK_SPACE)
            executeManagementAction();
          else if (key == SDLK_DELETE) {
            confirmationChoice = 0;
            managementDialog = ui::ManagementDialog::ConfirmRemoveRom;
          }
        } else if (managementDialog == ui::ManagementDialog::Info) {
          if (key == SDLK_ESCAPE || key == SDLK_b || key == SDLK_RETURN ||
              key == SDLK_SPACE)
            managementDialog = ui::ManagementDialog::Menu;
        } else {
          if (key == SDLK_ESCAPE || key == SDLK_b)
            managementDialog = ui::ManagementDialog::Menu;
          else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP ||
                   key == SDLK_DOWN)
            confirmationChoice = 1 - confirmationChoice;
          else if (key == SDLK_RETURN || key == SDLK_SPACE)
            confirmManagementAction();
        }
        break;
      }
      case SDL_MOUSEBUTTONDOWN: {
        float x = 0.0f;
        float y = 0.0f;
        ui::windowToCanvas(event.button.x, event.button.y, x, y);
        if (screen == Screen::Home) {
          const int clickedCard = ui::homeCardAt(
              x, y, static_cast<int>(gameLibrary.slotCount()), homeRow);
          const int clickedAction =
              ui::homeActionAt(x, y, selectedEntryAvailable(),
                               currentLanguage(), currentPrompts());
          if (event.button.button == SDL_BUTTON_RIGHT && clickedCard >= 0) {
            selectedGame = clickedCard;
            (void)openManagement(ui::ManagementDialog::Menu);
          } else if (event.button.button == SDL_BUTTON_LEFT) {
            const int clickedPill =
                ui::homePillAt(x, y, currentLanguage(),
                               gameLibrary.entries().size());
            if (clickedPill == 0) {
              chooseAndImportGame();
            } else if (clickedPill == 1) {
              openSettings();
            } else if (clickedAction >= 0) {
              homeFocus = ui::HomeFocus::Actions;
              homeAction = clickedAction;
              activateHomeAction();
            } else if (clickedCard >= 0) {
              homeFocus = ui::HomeFocus::Cards;
              const bool alreadySelected = clickedCard == selectedGame;
              selectedGame = clickedCard;
              if (alreadySelected)
                (void)startSelected();
            }
          }
        } else if (screen == Screen::Management) {
          if (event.button.button == SDL_BUTTON_RIGHT) {
            if (managementDialog == ui::ManagementDialog::Menu)
              screen = Screen::Home;
            else
              managementDialog = ui::ManagementDialog::Menu;
          } else if (event.button.button == SDL_BUTTON_LEFT &&
                     managementDialog == ui::ManagementDialog::Menu) {
            const int action = ui::managementActionAt(x, y);
            if (action >= 0) {
              managementAction = action;
              executeManagementAction();
            }
          } else if (event.button.button == SDL_BUTTON_LEFT &&
                     managementDialog == ui::ManagementDialog::Info) {
            managementDialog = ui::ManagementDialog::Menu;
          } else if (event.button.button == SDL_BUTTON_LEFT) {
            const int choice = ui::confirmationChoiceAt(x, y);
            if (choice >= 0) {
              confirmationChoice = choice;
              confirmManagementAction();
            }
          }
        } else if (ui::settingsCategoryAt(x, y) >= 0) {
          const int clickedCategory = ui::settingsCategoryAt(x, y);
          settingCategory = clickedCategory;
          settingRow = 0;
          settingsRailFocus = false;
          if (clickedCategory == 5)
            refreshCoreRows();
        } else if (settingCategory == 5 && ui::coreRefreshAt(x, y)) {
          refreshCoreRows();
        } else {
          const int clickedRow =
              ui::settingsRowAt(settingCategory, x, y);
          if (clickedRow >= 0) {
            settingRow = clickedRow;
            settingsRailFocus = false;
            if (settingCategory == 3 && settingRow >= 4 &&
                settingRow < 4 + static_cast<int>(config::kInputActionCount)) {
              bindingCapture = true;
            } else if (settingCategory == 5) {
              // A single click only moves the cursor: the list never starts a
              // download behind the player's back.
              if (event.button.clicks >= 2)
                activateCoreRow(clickedRow);
            } else {
              applySetting(1);
            }
          }
        }
        break;
      }
      case SDL_MOUSEWHEEL:
        if (screen == Screen::Home) {
          if (event.wheel.x < 0 || event.wheel.y < 0)
            ui::moveSelection(selectedGame, 1,
                              static_cast<int>(gameLibrary.slotCount()));
          else if (event.wheel.x > 0 || event.wheel.y > 0)
            ui::moveSelection(selectedGame, -1,
                              static_cast<int>(gameLibrary.slotCount()));
        } else if (screen == Screen::Settings && event.wheel.y != 0) {
          const int direction = event.wheel.y < 0 ? 1 : -1;
          if (settingsRailFocus) {
            changeCategory(direction);
          } else {
            const int count = ui::settingRowCount(settingCategory);
            settingRow = (settingRow + direction + count) % count;
          }
        } else if (screen == Screen::Management && event.wheel.y != 0 &&
                   managementDialog == ui::ManagementDialog::Menu) {
          const int direction = event.wheel.y < 0 ? 1 : -1;
          managementAction =
              (managementAction + direction + ui::kManagementActionCount) %
              ui::kManagementActionCount;
        }
        break;
      case SDL_CONTROLLERBUTTONDOWN:
        if (screen == Screen::Home) {
          const bool left =
              event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT;
          const bool right =
              event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
          if ((left || right) && homeFocus == ui::HomeFocus::Pills) {
            homePill = 1 - homePill;
          } else if ((left || right) && homeFocus == ui::HomeFocus::Actions) {
            const int count = ui::homeActionCount(selectedEntryAvailable());
            if (count > 0) {
              const int step = left ? count - 1 : 1;
              homeAction = (homeAction + step) % count;
            }
          } else if (left) {
            ui::moveSelection(selectedGame, -1,
                              static_cast<int>(gameLibrary.slotCount()));
          } else if (right) {
            ui::moveSelection(selectedGame, 1,
                              static_cast<int>(gameLibrary.slotCount()));
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            if (homeFocus == ui::HomeFocus::Cards)
              homeFocus = ui::HomeFocus::Pills;
            else if (homeFocus == ui::HomeFocus::Actions)
              homeFocus = ui::HomeFocus::Cards;
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            if (homeFocus == ui::HomeFocus::Pills) {
              homeFocus = ui::HomeFocus::Cards;
            } else if (homeFocus == ui::HomeFocus::Cards &&
                       ui::homeActionCount(selectedEntryAvailable()) > 0) {
              homeFocus = ui::HomeFocus::Actions;
              homeAction = 0;
            }
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            if (homeFocus == ui::HomeFocus::Pills) {
              if (homePill == 0)
                chooseAndImportGame();
              else
                openSettings();
            } else if (homeFocus == ui::HomeFocus::Actions) {
              activateHomeAction();
            } else {
              (void)startSelected();
            }
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
            (void)openManagement(ui::ManagementDialog::Menu);
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
                     event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
            quitConfirm = true;
            quitChoice = 0;
          } else if (event.cbutton.button ==
                         SDL_CONTROLLER_BUTTON_LEFTSHOULDER ||
                     event.cbutton.button ==
                         SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            const int count = static_cast<int>(gameLibrary.slotCount());
            const int direction =
                event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
                    ? 1
                    : -1;
            selectedGame =
                std::clamp(selectedGame + direction * 7, 0, count - 1);
          }
        } else if (screen == Screen::Settings) {
          const int rowCount = ui::settingRowCount(settingCategory);
          if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
              event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
            bindingCapture = false;
            if (settingsRailFocus)
              screen = Screen::Home;
            else
              settingsRailFocus = true;
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
            settingsRailFocus = true;
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            settingsRailFocus = false;
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            if (settingsRailFocus)
              changeCategory(-1);
            else
              settingRow = (settingRow - 1 + rowCount) % rowCount;
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            if (settingsRailFocus)
              changeCategory(1);
            else
              settingRow = (settingRow + 1) % rowCount;
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            if (settingsRailFocus) {
              settingsRailFocus = false;
            } else if (settingCategory == 5) {
              activateCoreRow(settingRow);
            } else if (settingCategory == 3 && settingRow >= 4 &&
                       settingRow <
                           4 + static_cast<int>(config::kInputActionCount)) {
              bindingCapture = true;
            } else {
              applySetting(1);
            }
          } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_Y &&
                     settingCategory == 3 && !settingsRailFocus) {
            settings.input = config::defaultInputBindings();
            if (activeRuntime)
              activeRuntime->setInputBindings(settings.input);
            saveSettings(settings);
          }
        } else if (managementDialog == ui::ManagementDialog::Menu) {
          if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
            managementAction =
                (managementAction - 1 + ui::kManagementActionCount) %
                ui::kManagementActionCount;
          else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            managementAction =
                (managementAction + 1) % ui::kManagementActionCount;
          else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
            executeManagementAction();
          else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
                   event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK)
            screen = Screen::Home;
        } else if (managementDialog == ui::ManagementDialog::Info) {
          if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A ||
              event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
              event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK)
            managementDialog = ui::ManagementDialog::Menu;
        } else {
          if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
              event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ||
              event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
              event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            confirmationChoice = 1 - confirmationChoice;
          else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
            confirmManagementAction();
          else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
                   event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK)
            managementDialog = ui::ManagementDialog::Menu;
        }
        break;
      case SDL_CONTROLLERAXISMOTION:
        if (SDL_GetTicks64() - lastAxisMove > 240) {
          const int direction = event.caxis.value < -22000
                                    ? -1
                                    : (event.caxis.value > 22000 ? 1 : 0);
          if (direction != 0 && event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
            if (screen == Screen::Home) {
              ui::moveSelection(selectedGame, direction,
                                static_cast<int>(gameLibrary.slotCount()));
            } else if (screen == Screen::Settings) {
              applySetting(direction);
            } else if (screen == Screen::Management &&
                       managementDialog != ui::ManagementDialog::Menu &&
                       managementDialog != ui::ManagementDialog::Info) {
              confirmationChoice = direction > 0 ? 1 : 0;
            }
            lastAxisMove = SDL_GetTicks64();
          } else if (direction != 0 &&
                     event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            if (screen == Screen::Settings) {
              if (settingsRailFocus) {
                changeCategory(direction);
              } else {
                const int count = ui::settingRowCount(settingCategory);
                settingRow = (settingRow + direction + count) % count;
              }
            } else if (screen == Screen::Management &&
                       managementDialog == ui::ManagementDialog::Menu) {
              managementAction =
                  (managementAction + direction + ui::kManagementActionCount) %
                  ui::kManagementActionCount;
            }
            lastAxisMove = SDL_GetTicks64();
          }
        }
        break;
      default:
        break;
      }
    }

    const float time =
        static_cast<float>((SDL_GetPerformanceCounter() - start) / frequency);
    const config::InputPrompts prompts = currentPrompts();
    // One ring, one control: the key names whichever control currently owns
    // the focus, and moving to a different one restarts the fade.
    char focusBuffer[80];
    std::snprintf(focusBuffer, sizeof(focusBuffer),
                  "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
                  static_cast<int>(screen), static_cast<int>(homeFocus),
                  homePill, homeAction, selectedGame, settingCategory,
                  settingRow, settingsRailFocus ? 1 : 0, bindingCapture ? 1 : 0,
                  pauseAction, pauseCoreOptionCursor);
    if (focusKey != focusBuffer) {
      if (focusKey.empty()) {
        focusOpacity = 1.0f;
      } else {
        focusOpacity = 0.0f;
        focusSince = SDL_GetTicks64();
      }
      focusKey = focusBuffer;
    }
    if (focusOpacity < 1.0f) {
      const float elapsed =
          static_cast<float>(SDL_GetTicks64() - focusSince) / 120.0f;
      const float progress = std::min(1.0f, elapsed);
      focusOpacity = 1.0f - (1.0f - progress) * (1.0f - progress);
    }
    if (screen == Screen::Home) {
      // The shelf eases towards the column that keeps the selection on screen,
      // so a card sliding past never jumps.
      const float rowTarget = ui::homeRowTarget(
          selectedGame, static_cast<int>(gameLibrary.slotCount()));
      // Coming back from another screen should show the shelf where it belongs
      // instead of sliding in from the start.
      if (!homeRowSettled) {
        homeRow = rowTarget;
        homeRowSettled = true;
      }
      homeRow += (rowTarget - homeRow) * 0.35f;
      if (std::abs(rowTarget - homeRow) < 0.002f)
        homeRow = rowTarget;
    } else {
      homeRowSettled = false;
    }
    // The highlighted game is the one being looked at, so its art jumps the
    // queue; the shelf's left-to-right order fills in after it.
    if (screen == Screen::Home || selectedEntryAvailable()) {
      const auto &shelf = gameLibrary.entries();
      if (selectedGame < static_cast<int>(shelf.size())) {
        const auto index = static_cast<std::size_t>(selectedGame);
        (void)coverCache.cover(renderer, shelf[index]);
      }
    }
    // Downloads and decodes finish here, never inside a draw call.
    coverCache.poll(renderer);
    // The interface draws itself in output pixels, so every frame starts by
    // telling the UI how big the window's framebuffer is.
    int outputWidth = 0;
    int outputHeight = 0;
    if (SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight) == 0)
      ui::setOutputSize(outputWidth, outputHeight);
    // Mouse positions arrive in window points, which are not the pixels above
    // when the display scales the window, so the pointer mapping gets both.
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    ui::setWindowSize(windowWidth, windowHeight);
    // Nothing paints the bars around the canvas, so clear the whole
    // framebuffer; otherwise a window that is not exactly 16:9 shows whatever
    // the framebuffer held.
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (screen == Screen::Game) {
      if (activeRuntime) {
        // Holding the rewind key stops the clock and walks the snapshots
        // backwards; releasing it resumes from the restored point in time.
        if (rewindHeld)
          (void)activeRuntime->rewindStep();
        else
          activeRuntime->update();
        activeRuntime->render(renderer);
      }
    } else if (screen == Screen::Pause) {
      if (activeRuntime) {
        activeRuntime->render(renderer);
        const SDL_Color accent = ui::accentColor(settings.accent);
        if (pauseCoreOptionsPanel) {
          ui::drawPauseCoreOptionsPanel(renderer, pauseCoreOptionCursor,
                                        pauseCoreOptionScroll,
                                        activeRuntime->coreOptions(),
                                        currentLanguage(), prompts, accent);
        } else if (pauseVideoPanel) {
          ui::drawPauseVideoPanel(renderer, pauseVideoRow, pauseVideo,
                                  currentLanguage(), prompts, accent);
        } else {
          ui::drawPauseMenu(renderer, activeGameTitle, pauseAction, stateSlot,
                            stateSlots, pauseFeedback, pauseFeedbackUntil,
                            currentLanguage(), prompts, accent);
        }
        if (pauseConfirmingPower)
          ui::drawPowerCycleConfirm(renderer, pausePowerChoice,
                                    currentLanguage(), prompts, accent);
      }
    } else if (screen == Screen::Home) {
      ui::drawHome(renderer, selectedGame, time, toastUntil, toastMessage,
                   settings.startupPage, currentLanguage(), gameLibrary,
                   prompts, homeFocus, homePill, homeAction, homeRow,
                   focusOpacity, ui::accentColor(settings.accent), &coverCache);
    } else if (screen == Screen::Settings) {
      ui::drawSettings(renderer, settings, settingCategory, settingRow, time,
                       bindingCapture, activeControllerName(),
                       resolvedControllerStyle(), coreRows, prompts,
                       settingsRailFocus, focusOpacity);
    } else if (selectedEntryAvailable()) {
      ui::drawHome(renderer, selectedGame, time, toastUntil, toastMessage,
                   settings.startupPage, currentLanguage(), gameLibrary,
                   prompts, homeFocus, homePill, homeAction, homeRow,
                   focusOpacity, ui::accentColor(settings.accent), &coverCache,
                   /*showFooter=*/false);
      ui::drawGameManagementOverlay(
          renderer,
          gameLibrary.entries()[static_cast<std::size_t>(selectedGame)],
          managementAction, managementDialog, confirmationChoice,
          saveDataPresent, currentLanguage(), prompts);
    }

    if (quitConfirm) {
      ui::drawQuitConfirm(renderer, quitChoice, currentLanguage(), prompts,
                          ui::accentColor(settings.accent));
    }

    if ((screen == Screen::Game || screen == Screen::Pause ||
         screen == Screen::Settings) &&
        !bannerMessage.empty() && SDL_GetTicks64() < bannerUntil) {
      const int bannerWidth = ui::textWidth(bannerMessage, 1) + 44;
      const float bannerX =
          (static_cast<float>(ui::kLogicalWidth) - bannerWidth) * 0.5f;
      ui::rounded(renderer, bannerX, 30.0f, static_cast<float>(bannerWidth),
                  44.0f, 22.0f, {8, 16, 31, 230});
      ui::centeredText(renderer, ui::kLogicalWidth / 2, 42, bannerMessage, 1,
                       {255, 255, 255, 255});
    }

#if defined(MRG_ENABLE_SMOKE_TESTS)
    if (options.smokeRom && screen == Screen::Game && !smokeRomCaptured && ++smokeRomFrames >= 3) {
      captureFrame(renderer, "emulator-screenshot.bmp");
      smokeRomCaptured = true;
    }
    else if (options.smokeHome && screen == Screen::Home) {
      // Two shots: the row on its own, then the footer with a focused button,
      // so the smoke test covers the focus ring as well as the layout.
      // Each shot waits for the focus fade to finish, otherwise the ring in the
      // capture would be caught half transparent.
      if (focusOpacity < 1.0f) {
        // still fading
      } else if (!smokeHomePills) {
        captureFrame(renderer, "launcher-screenshot.bmp");
        smokeHomePills = true;
        homeFocus = ui::HomeFocus::Pills;
        homePill = 1;
      } else if (!smokeHomeActions) {
        captureFrame(renderer, "home-pills-screenshot.bmp");
        smokeHomeActions = true;
        homeFocus = ui::HomeFocus::Actions;
        homeAction = 1;
      } else if (!smokeHomeQuit) {
        captureFrame(renderer, "home-actions-screenshot.bmp");
        smokeHomeQuit = true;
        quitConfirm = true;
        quitChoice = 0;
      } else {
        captureFrame(renderer, "home-quit-screenshot.bmp");
        quitConfirm = false;
        smokeHomeDone = true;
      }
    }
    else if (options.smokeSettings && screen == Screen::Settings) {
      // Wait for the entry animation to settle before capturing, then switch
      // to the display page so the video options are covered too.
      const Uint64 elapsed = SDL_GetTicks64() - smokeSettingsStart;
      if (!smokeSettingsGeneral && elapsed >= 600) {
        captureFrame(renderer, "settings-screenshot.bmp");
        smokeSettingsGeneral = true;
        settingCategory = 1;
        settingRow = 1;
      } else if (smokeSettingsGeneral && !smokeSettingsDisplay &&
                 elapsed >= 1200) {
        captureFrame(renderer, "settings-display-screenshot.bmp");
        smokeSettingsDisplay = true;
        settingCategory = 2;
        settingRow = 1;
      } else if (smokeSettingsDisplay && !smokeSettingsAudio &&
                 elapsed >= 1800) {
        captureFrame(renderer, "settings-audio-screenshot.bmp");
        smokeSettingsAudio = true;
        // The controller page is the densest one: two columns of bindings.
        settingCategory = 3;
        settingRow = 4;
      } else if (smokeSettingsAudio && !smokeSettingsController &&
                 elapsed >= 2400) {
        captureFrame(renderer, "settings-controller-screenshot.bmp");
        smokeSettingsController = true;
        settingCategory = 5;
        settingRow = 0;
        refreshCoreRows();
      } else if (smokeSettingsController && !smokeSettingsCores &&
                 elapsed >= 3000) {
        captureFrame(renderer, "settings-cores-screenshot.bmp");
        smokeSettingsCores = true;
        settingCategory = 6;
        settingRow = 0;
      } else if (smokeSettingsCores && !smokeSettingsTheme && elapsed >= 3400) {
        captureFrame(renderer, "settings-theme-screenshot.bmp");
        smokeSettingsTheme = true;
        settingCategory = 7;
        settingRow = 0;
      } else if (smokeSettingsTheme && !smokeSettingsAbout &&
                 elapsed >= 3800) {
        captureFrame(renderer, "settings-about-screenshot.bmp");
        smokeSettingsAbout = true;
      }
    }
    else if (options.smokeManagement && screen == Screen::Management)
      captureFrame(renderer, "management-screenshot.bmp");
    else if (options.smokePause && screen == Screen::Pause) {
      if (!smokePauseCard && !pauseVideoPanel) {
        captureFrame(renderer, "pause-screenshot.bmp");
        if (options.smokeScreenshot)
          screenshotRequested = true;
        smokePauseCard = true;
        // The display panel lives behind the pause card, so the same run
        // proves both screens render.
        pauseAction = 5;
        executePauseAction();
      } else if (smokePauseCard && pauseVideoPanel) {
        captureFrame(renderer, "pause-display-screenshot.bmp");
        smokePauseVideo = true;
        // The options panel sits behind the same action, so the run proves
        // both cards render.
        pauseVideoPanel = false;
        pauseAction = 6;
        executePauseAction();
      } else if (smokePauseVideo && pauseCoreOptionsPanel) {
        captureFrame(renderer, "pause-options-screenshot.bmp");
        smokePauseCoreOptions = true;
      }
    }
#endif
    if (screenshotRequested) {
      screenshotRequested = false;
      (void)saveScreenshot();
    }
    SDL_RenderPresent(renderer);
#if defined(MRG_ENABLE_SMOKE_TESTS)
    if (options.smokeTest() &&
        (!options.smokeSettings || smokeSettingsAbout) &&
        (!options.smokeHome || smokeHomeDone) &&
        (!options.smokePause ||
         smokePauseCoreOptions || screen != Screen::Pause) &&
        (!options.smokeRom || smokeRomCaptured))
      running = false;
    else
#endif
    if (screen == Screen::Game || screen == Screen::Pause)
      SDL_Delay(1);
  }

  if (coreInstallThread != nullptr) {
    SDL_WaitThread(coreInstallThread, nullptr);
    coreInstallThread = nullptr;
  }
  if (activeRuntime) {
    recordCurrentSession();
    activeRuntime->stop();
  }
  if (settings.rememberSelection)
    settings.selectedGame = selectedGame;
  if (options.languageOverride < 0)
    saveSettings(settings);
  if (controller != nullptr)
    SDL_GameControllerClose(controller);
  if (controller2 != nullptr)
    SDL_GameControllerClose(controller2);
  clearStateSlots();
  // Cached textures belong to this renderer, so drop them first.
  coverCache.shutdown();
  ui::shutdownTextRenderer();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return startupRomFailed ? 1 : 0;
}

} // namespace launcher
