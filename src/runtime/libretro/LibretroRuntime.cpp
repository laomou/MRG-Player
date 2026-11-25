#include "runtime/libretro/LibretroRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#include <libretro.h>

#include "config/AppPaths.hpp"
#include "config/InputBindings.hpp"
#include "game/GamePlatform.hpp"
#include "library/GameLibrary.hpp"
#include "runtime/VideoViewport.hpp"
#include "runtime/libretro/CoreDiscovery.hpp"
#include "util/ImageFile.hpp"
#include "util/Sha256.hpp"
#include "util/Utf8Path.hpp"

namespace launcher::runtime::libretro {
namespace {
namespace fs = std::filesystem;

constexpr const char *kStateMagic = "MRGLIBRETRO1";
constexpr std::size_t kMaximumRomSize = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumSaveRamSize = 1024U * 1024U;
constexpr std::size_t kMaximumStateSize = 64U * 1024U * 1024U;
// A core may announce a picture of any size before it draws one. Refusing past
// this keeps a hostile or broken core from asking for an unbounded buffer.
constexpr std::size_t kMaximumFramePixels = 4096U * 4096U;
constexpr std::uint32_t kFastForwardDivisor = 4;
constexpr std::uint32_t kSlowMotionFactor = 2;
constexpr int kAudioBufferSamples = 1024;
// Rewind trades memory for depth: a snapshot every other frame, capped at
// roughly ten seconds of play or 32 MiB of serialized machine state.
constexpr std::uint64_t kRewindIntervalFrames = 2;
constexpr std::size_t kRewindMaximumSnapshots = 300;
constexpr std::size_t kRewindMaximumBytes = 32U * 1024U * 1024U;
constexpr int kMaximumRunaheadFrames = 3;

// The libretro joypad id every config::InputAction drives, in enum order. Turbo
// is a front-end pulse and has no id of its own, so it is marked instead of
// borrowing one: a shared id would make the turbo keys press a real button.
constexpr unsigned kNoCoreButton = ~0U;
constexpr std::array<unsigned, config::kInputActionCount> kActionButtons{
    RETRO_DEVICE_ID_JOYPAD_A,      RETRO_DEVICE_ID_JOYPAD_B,
    RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START,
    RETRO_DEVICE_ID_JOYPAD_UP,     RETRO_DEVICE_ID_JOYPAD_DOWN,
    RETRO_DEVICE_ID_JOYPAD_LEFT,   RETRO_DEVICE_ID_JOYPAD_RIGHT,
    kNoCoreButton,                 kNoCoreButton,
    RETRO_DEVICE_ID_JOYPAD_X,      RETRO_DEVICE_ID_JOYPAD_Y,
    RETRO_DEVICE_ID_JOYPAD_L,      RETRO_DEVICE_ID_JOYPAD_R};

bool readFile(const std::string &path, std::vector<std::uint8_t> &bytes, std::size_t limit = kMaximumRomSize) {
  std::ifstream input(paths::fromUtf8(path), std::ios::binary | std::ios::ate);
  if (!input)
    return false;
  const std::streamoff size = input.tellg();
  if (size <= 0 || static_cast<std::uint64_t>(size) > limit)
    return false;
  bytes.resize(static_cast<std::size_t>(size));
  input.seekg(0);
  return static_cast<bool>(input.read(reinterpret_cast<char *>(bytes.data()), size));
}

// Cores open the ROM themselves through narrow file APIs, so a UTF-8 name such
// as "F1赛车.nes" is read back in the ANSI code page on Windows and the open
// fails. Hand every such game a copy with an ASCII name inside our own data
// directory: it costs one file per launch and keeps the rule the same on every
// platform.
bool coreNeedsStagedRomPath(const std::string &path) {
  return std::any_of(path.begin(), path.end(),
                     [](unsigned char ch) { return ch >= 0x80; });
}

bool stageRomFile(const std::vector<std::uint8_t> &bytes,
                  const std::string &sourcePath, std::string &stagedPath,
                  std::string &error) {
  const std::string hash = util::sha256Hex(bytes.data(), bytes.size());
  const std::string extension =
      paths::toUtf8(paths::fromUtf8(sourcePath).extension());
  const fs::path cache = paths::fromUtf8(
      config::dataSubdirectory("rom-cache", ""));
  const fs::path target = cache / (hash.substr(0, 16) + extension);
  std::ofstream output(target, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "Cannot stage the game file for the core: " + paths::toUtf8(target);
    return false;
  }
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  if (!output) {
    error = "Cannot stage the game file for the core: " + paths::toUtf8(target);
    return false;
  }
  stagedPath = paths::toUtf8(target);
  return true;
}

std::string trim(const std::string &value) {
  const std::size_t first = value.find_first_not_of(" \t");
  if (first == std::string::npos)
    return {};
  const std::size_t last = value.find_last_not_of(" \t");
  return value.substr(first, last - first + 1);
}

void writeTextFile(const std::string &path, const std::string &text) {
  std::ofstream output(paths::fromUtf8(path),
                       std::ios::binary | std::ios::trunc);
  if (output)
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void logMessage(enum retro_log_level level, const char *format, ...) {
  static const char *const kLevels[] = {"debug", "info", "warn", "error"};
  const char *name = level <= RETRO_LOG_ERROR ? kLevels[level] : "log";
  std::fprintf(stderr, "[core %s] ", name);
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
}

} // namespace

LibretroRuntime::LibretroRuntime() { pixelFormat_ = RETRO_PIXEL_FORMAT_0RGB1555; }

LibretroRuntime::~LibretroRuntime() { stop(); }

bool LibretroRuntime::load(const library::GameEntry &game, SDL_Renderer *renderer, int volume) {
  stop();
  error_.clear();
  if (!readFile(game.filePath, romBytes_)) {
    error_ = "Cannot read game file: " + game.filePath;
    return false;
  }
  const std::string corePath = findCore(game.platform);
  if (corePath.empty()) {
    error_ = "No libretro core is installed for " + std::string(game::platformDescriptor(game.platform).displayName);
    romBytes_.clear();
    return false;
  }
  if (!core_.load(corePath, error_)) {
    romBytes_.clear();
    return false;
  }
  name_ = core_.libraryName();
  if (!core_.libraryVersion().empty())
    name_ += " " + core_.libraryVersion();
  const std::string coreStem = paths::toUtf8(paths::fromUtf8(corePath).stem());
  optionsPath_ =
      paths::toUtf8(paths::fromUtf8(config::dataSubdirectory("core-options", "")) /
                    (coreStem + ".tsv"));
  loadCoreOptionValues();
  platform_ = game.platform;
  gameId_ = game.id;
  gamePath_ = game.filePath;
  renderer_ = renderer;
  const std::string directory = std::string(game::platformDescriptor(game.platform).managedDirectory);
  savePath_ = paths::toUtf8(
      paths::fromUtf8(config::dataSubdirectory("saves", directory)) /
      paths::fromUtf8(game.id + ".srm"));
  stateDirectory_ = config::dataSubdirectory("states", directory);
  systemDirectory_ =
      paths::toUtf8(paths::fromUtf8(config::dataRoot()) / "system");
  {
    std::error_code ignored;
    fs::create_directories(paths::fromUtf8(systemDirectory_), ignored);
  }
  saveDirectory_ = config::dataSubdirectory("saves", directory);

  LibretroCore::Host host;
  host.environment = [this](unsigned command, void *data) { return onEnvironment(command, data); };
  host.video = [this](const void *data, unsigned width, unsigned height, std::size_t pitch) {
    onVideo(data, width, height, pitch);
  };
  host.audio = [this](const std::int16_t *data, std::size_t frames) { return onAudio(data, frames); };
  host.input = [this](unsigned port, unsigned device, unsigned index, unsigned id) {
    return onInput(port, device, index, id);
  };
  core_.attachEnvironment(std::move(host));
  core_.init();
  core_.attachCallbacks();
  romLoadPath_ = game.filePath;
  if (coreNeedsStagedRomPath(romLoadPath_) &&
      !stageRomFile(romBytes_, game.filePath, stagedRomPath_, error_)) {
    stop();
    return false;
  }
  if (!stagedRomPath_.empty())
    romLoadPath_ = stagedRomPath_;
  const bool fullPath = core_.needsFullPath();
  const void *data = fullPath ? nullptr : romBytes_.data();
  const std::size_t size = fullPath ? 0 : romBytes_.size();
  if (!core_.loadGame(romLoadPath_.c_str(), data, size)) {
    error_ = "The core cannot load " + game.filePath;
    stop();
    return false;
  }
  gameLoaded_ = true;
  core_.setControllerPortDevice(0, RETRO_DEVICE_JOYPAD);
  core_.setControllerPortDevice(1, RETRO_DEVICE_JOYPAD);

  const retro_system_av_info info = core_.avInfo();
  width_ = info.geometry.base_width;
  height_ = info.geometry.base_height;
  fps_ = info.timing.fps > 0.0 ? info.timing.fps : 60.0;
  sampleRate_ = info.timing.sample_rate > 0.0 ? static_cast<int>(info.timing.sample_rate) : 44100;
  volume_ = std::clamp(volume, 0, 100);
  restoreSaveRam();
  openAudio();
  const Uint64 frequency = SDL_GetPerformanceFrequency();
  framePeriod_ = static_cast<Uint64>(static_cast<double>(frequency) / fps_);
  nextFrame_ = SDL_GetPerformanceCounter();
  resetPlayers();
  clearRewind();
  paused_ = false;
  active_ = true;
  return true;
}

void LibretroRuntime::stop() {
  if (active_)
    writeSaveRam();
  active_ = false;
  paused_ = false;
  fastForward_ = false;
  releaseInput();
  closeAudio();
  if (core_.valid()) {
    if (gameLoaded_)
      core_.unloadGame();
    core_.deinit();
    core_.unload();
  }
  gameLoaded_ = false;
  if (texture_ != nullptr)
    SDL_DestroyTexture(texture_);
  texture_ = nullptr;
  renderer_ = nullptr;
  if (!stagedRomPath_.empty()) {
    std::error_code ignored;
    fs::remove(paths::fromUtf8(stagedRomPath_), ignored);
  }
  stagedRomPath_.clear();
  romLoadPath_.clear();
  gameId_.clear();
  gamePath_.clear();
  savePath_.clear();
  stateDirectory_.clear();
  systemDirectory_.clear();
  saveDirectory_.clear();
  romBytes_.clear();
  frame_.clear();
  stateBuffer_.clear();
  clearRewind();
  options_.clear();
  optionsPath_.clear();
  storedOptions_.clear();
  optionsChanged_ = false;
  name_.clear();
  platform_ = game::GamePlatform::Unknown;
  width_ = 0;
  height_ = 0;
  framePeriod_ = 0;
  nextFrame_ = 0;
  frameCount_ = 0;
  controllerIds_.fill(0);
}

bool LibretroRuntime::rebuildGame() {
  if (!core_.valid() || romBytes_.empty())
    return false;
  core_.unloadGame();
  gameLoaded_ = false;
  const bool fullPath = core_.needsFullPath();
  const std::string &path = romLoadPath_.empty() ? gamePath_ : romLoadPath_;
  if (!core_.loadGame(path.c_str(), fullPath ? nullptr : romBytes_.data(),
                      fullPath ? 0 : romBytes_.size()))
    return false;
  gameLoaded_ = true;
  core_.setControllerPortDevice(0, RETRO_DEVICE_JOYPAD);
  core_.setControllerPortDevice(1, RETRO_DEVICE_JOYPAD);
  return true;
}

void LibretroRuntime::restoreSaveRam() {
  void *data = core_.memoryData(RETRO_MEMORY_SAVE_RAM);
  const std::size_t size = core_.memorySize(RETRO_MEMORY_SAVE_RAM);
  if (data == nullptr || size == 0 || size > kMaximumSaveRamSize)
    return;
  std::ifstream input(paths::fromUtf8(savePath_),
                      std::ios::binary | std::ios::ate);
  if (!input)
    return;
  const std::streamoff bytes = input.tellg();
  if (bytes <= 0 || static_cast<std::uint64_t>(bytes) > size)
    return;
  input.seekg(0);
  (void)input.read(static_cast<char *>(data), bytes);
}

void LibretroRuntime::writeSaveRam() const {
  if (savePath_.empty())
    return;
  const void *data = core_.memoryData(RETRO_MEMORY_SAVE_RAM);
  const std::size_t size = core_.memorySize(RETRO_MEMORY_SAVE_RAM);
  if (data == nullptr || size == 0 || size > kMaximumSaveRamSize)
    return;
  std::ofstream output(paths::fromUtf8(savePath_), std::ios::binary | std::ios::trunc);
  if (output)
    output.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
}

void LibretroRuntime::openAudio() {
  SDL_AudioSpec wanted{};
  wanted.freq = sampleRate_;
  wanted.format = AUDIO_S16SYS;
  wanted.channels = 2;
  wanted.samples = kAudioBufferSamples;
  SDL_AudioSpec obtained{};
  audioDevice_ = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (audioDevice_ == 0)
    return;
  deviceRate_ = obtained.freq;
  if (deviceRate_ != sampleRate_) {
    resampler_ = SDL_NewAudioStream(AUDIO_S16SYS, 2, sampleRate_, AUDIO_S16SYS, 2, deviceRate_);
  }
  SDL_PauseAudioDevice(audioDevice_, 0);
}

void LibretroRuntime::closeAudio() {
  if (resampler_ != nullptr)
    SDL_FreeAudioStream(resampler_);
  resampler_ = nullptr;
  if (audioDevice_ == 0)
    return;
  SDL_ClearQueuedAudio(audioDevice_);
  SDL_CloseAudioDevice(audioDevice_);
  audioDevice_ = 0;
}

void LibretroRuntime::setVolume(int volume) noexcept { volume_ = std::clamp(volume, 0, 100); }

void LibretroRuntime::setMuted(bool muted) noexcept {
  if (muted_ == muted)
    return;
  muted_ = muted;
  if (audioDevice_ != 0)
    SDL_ClearQueuedAudio(audioDevice_);
}

void LibretroRuntime::setFastForwardAudio(bool keep) noexcept {
  if (fastForwardAudio_ == keep)
    return;
  fastForwardAudio_ = keep;
  fastForwardFrames_ = 0;
}

void LibretroRuntime::setInputBindings(const config::InputBindings &bindings) {
  bindings_ = bindings;
  bindings_.analogDeadZone = std::clamp(bindings_.analogDeadZone, 4000, 30000);
  releaseInput();
}

void LibretroRuntime::setPlayerController(int player, SDL_JoystickID instanceId) noexcept {
  if (player < 0 || player >= static_cast<int>(config::kPlayerCount))
    return;
  const std::size_t index = static_cast<std::size_t>(player);
  if (controllerIds_[index] == instanceId)
    return;
  inputMask_[index] = 0;
  axes_[index] = AxisState{};
  controllerIds_[index] = instanceId;
}

int LibretroRuntime::playerForController(SDL_JoystickID instanceId) const noexcept {
  for (std::size_t player = 0; player < config::kPlayerCount; ++player) {
    if (controllerIds_[player] == instanceId)
      return static_cast<int>(player);
  }
  // Unassigned pads drive port 1 so a single controller always plays.
  return 0;
}

void LibretroRuntime::handleEvent(const SDL_Event &event) {
  if (!active_ || paused_)
    return;
  if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) && event.key.repeat == 0) {
    const bool pressed = event.type == SDL_KEYDOWN;
    for (std::size_t player = 0; player < config::kPlayerCount; ++player) {
      const auto &keys = config::keyboardBindings(bindings_, player);
      for (std::size_t index = 0; index < config::kInputActionCount; ++index) {
        if (event.key.keysym.sym == keys[index]) {
          setAction(static_cast<int>(player), static_cast<config::InputAction>(index), pressed);
          return;
        }
      }
    }
  } else if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
    const int player = playerForController(event.cbutton.which);
    const bool pressed = event.type == SDL_CONTROLLERBUTTONDOWN;
    const auto &pads = config::controllerBindings(bindings_, static_cast<std::size_t>(player));
    for (std::size_t index = 0; index < config::kInputActionCount; ++index) {
      if (event.cbutton.button == pads[index]) {
        setAction(player, static_cast<config::InputAction>(index), pressed);
        return;
      }
    }
  } else if (event.type == SDL_CONTROLLERAXISMOTION) {
    setControllerAxis(playerForController(event.caxis.which), static_cast<SDL_GameControllerAxis>(event.caxis.axis),
                     event.caxis.value);
  }
}

void LibretroRuntime::setAction(int player, config::InputAction action, bool pressed) noexcept {
  if (player < 0 || player >= static_cast<int>(config::kPlayerCount))
    return;
  const std::size_t index = static_cast<std::size_t>(action);
  if (index >= config::kInputActionCount)
    return;
  const std::uint16_t bit = static_cast<std::uint16_t>(1U << index);
  std::uint16_t &mask = inputMask_[static_cast<std::size_t>(player)];
  if (pressed)
    mask = static_cast<std::uint16_t>(mask | bit);
  else
    mask = static_cast<std::uint16_t>(mask & static_cast<std::uint16_t>(~bit));
  // The joypad mask is rebuilt from inputMask_ on every core query, so a turbo
  // release needs no extra work here.
}

void LibretroRuntime::resetPlayers() noexcept {
  inputMask_.fill(0);
  axes_.fill(AxisState{});
}

void LibretroRuntime::releaseInput() noexcept { resetPlayers(); }

void LibretroRuntime::setControllerAxis(int player, SDL_GameControllerAxis axis, Sint16 value) {
  if (player < 0 || player >= static_cast<int>(config::kPlayerCount))
    return;
  AxisState &state = axes_[static_cast<std::size_t>(player)];
  const Sint16 deadZone = static_cast<Sint16>(bindings_.analogDeadZone);
  if (axis == SDL_CONTROLLER_AXIS_LEFTX) {
    const bool left = value < -deadZone;
    const bool right = value > deadZone;
    if (left != state.left)
      setAction(player, config::InputAction::Left, left);
    if (right != state.right)
      setAction(player, config::InputAction::Right, right);
    state.left = left;
    state.right = right;
  } else if (axis == SDL_CONTROLLER_AXIS_LEFTY) {
    const bool up = value < -deadZone;
    const bool down = value > deadZone;
    if (up != state.up)
      setAction(player, config::InputAction::Up, up);
    if (down != state.down)
      setAction(player, config::InputAction::Down, down);
    state.up = up;
    state.down = down;
  }
}

std::uint16_t LibretroRuntime::joypadMask(int player) const noexcept {
  const std::size_t port = static_cast<std::size_t>(player);
  const std::uint16_t held = inputMask_[port];
  std::uint16_t mask = 0;
  for (std::size_t index = 0; index < config::kInputActionCount; ++index) {
    if (kActionButtons[index] == kNoCoreButton)
      continue;
    if ((held & static_cast<std::uint16_t>(1U << index)) != 0)
      mask = static_cast<std::uint16_t>(mask | (1U << kActionButtons[index]));
  }
  const auto holds = [held](config::InputAction action) noexcept {
    return (held & static_cast<std::uint16_t>(1U << static_cast<std::size_t>(action))) != 0;
  };
  if (turboPulse_ && holds(config::InputAction::TurboA))
    mask = static_cast<std::uint16_t>(mask | (1U << RETRO_DEVICE_ID_JOYPAD_A));
  if (turboPulse_ && holds(config::InputAction::TurboB))
    mask = static_cast<std::uint16_t>(mask | (1U << RETRO_DEVICE_ID_JOYPAD_B));
  return mask;
}

std::int16_t LibretroRuntime::onInput(unsigned port, unsigned device, unsigned index, unsigned id) {
  if (port >= config::kPlayerCount || device != RETRO_DEVICE_JOYPAD)
    return 0;
  (void)index;
  const std::uint16_t mask = joypadMask(static_cast<int>(port));
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
    return static_cast<std::int16_t>(mask);
  if (id >= 16)
    return 0;
  return static_cast<std::int16_t>((mask >> id) & 1U);
}

bool LibretroRuntime::onEnvironment(unsigned command, void *data) {
  switch (command) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
      if (data == nullptr)
        return false;
      *static_cast<bool *>(data) = true;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
      if (data == nullptr)
        return false;
      const auto format = *static_cast<const enum retro_pixel_format *>(data);
      if (format != RETRO_PIXEL_FORMAT_0RGB1555 && format != RETRO_PIXEL_FORMAT_XRGB8888 &&
          format != RETRO_PIXEL_FORMAT_RGB565)
        return false;
      pixelFormat_ = static_cast<unsigned>(format);
      return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
      if (data == nullptr)
        return false;
      const auto *info = static_cast<const retro_system_av_info *>(data);
      if (info->timing.fps > 0.0)
        fps_ = info->timing.fps;
      if (info->timing.sample_rate > 0.0)
        sampleRate_ = static_cast<int>(info->timing.sample_rate);
      if (info->geometry.base_width != 0)
        width_ = info->geometry.base_width;
      if (info->geometry.base_height != 0)
        height_ = info->geometry.base_height;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
      if (data == nullptr)
        return false;
      const auto *geometry = static_cast<const retro_game_geometry *>(data);
      if (geometry->base_width != 0)
        width_ = geometry->base_width;
      if (geometry->base_height != 0)
        height_ = geometry->base_height;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
      if (data == nullptr)
        return false;
      *static_cast<const char **>(data) = systemDirectory_.c_str();
      return true;
    }
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
      if (data == nullptr)
        return false;
      *static_cast<const char **>(data) = saveDirectory_.c_str();
      return true;
    }
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: {
      if (data == nullptr)
        return false;
      *static_cast<const char **>(data) = saveDirectory_.c_str();
      return true;
    }
    case RETRO_ENVIRONMENT_GET_OVERSCAN: {
      if (data == nullptr)
        return false;
      *static_cast<bool *>(data) = true;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: {
      if (data == nullptr)
        return false;
      *static_cast<unsigned *>(data) = RETRO_LANGUAGE_ENGLISH;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: {
      if (data == nullptr)
        return false;
      *static_cast<int *>(data) = 3;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING: {
      if (data == nullptr)
        return false;
      *static_cast<bool *>(data) = fastForward_;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
      if (data == nullptr)
        return false;
      *static_cast<bool *>(data) = optionsChanged_;
      optionsChanged_ = false;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      auto *variable = static_cast<retro_variable *>(data);
      if (variable == nullptr || variable->key == nullptr)
        return false;
      for (const CoreOption &option : options_) {
        if (option.key.compare(variable->key) != 0 || option.values.empty())
          continue;
        const std::size_t index = std::min(option.selected, option.values.size() - 1);
        variable->value = option.values[index].value.c_str();
        return true;
      }
      // An unknown key is answered with undefined, never with a guess.
      return false;
    }
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: {
      if (data == nullptr)
        return false;
      *static_cast<unsigned *>(data) = 2;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
      return declareCoreOptionsV1(static_cast<const retro_variable *>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
      // Version 1 still has two declaration shapes: the old key/description
      // strings and the definition array with labels and defaults.
      return declareCoreOptionsV1Definitions(
          static_cast<const retro_core_option_definition *>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      return declareCoreOptionsV2(static_cast<const retro_core_options_v2 *>(data));
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
      const auto *intl = static_cast<const retro_core_options_v2_intl *>(data);
      return intl != nullptr && intl->us != nullptr && declareCoreOptionsV2(intl->us);
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
      const auto *intl = static_cast<const retro_core_options_intl *>(data);
      return intl != nullptr && intl->us != nullptr && declareCoreOptionsV1Definitions(intl->us);
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      return data != nullptr;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
      if (data == nullptr)
        return false;
      static_cast<retro_log_callback *>(data)->log = &logMessage;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_MESSAGE:
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
      return true;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: {
      if (data == nullptr)
        return false;
      *static_cast<unsigned *>(data) = RETRO_HW_CONTEXT_NONE;
      return false;
    }
    default:
      return false;
  }
}

void LibretroRuntime::onVideo(const void *data, unsigned width, unsigned height, std::size_t pitch) {
  // Runahead runs throw-away frames before the one that is shown; they must
  // not reach the texture at all.
  if (suppressVideo_)
    return;
  if (width == 0 || height == 0)
    return;
  // The buffer is sized from the frame the core just handed over, never from
  // the geometry it announced earlier: SET_GEOMETRY may advertise a larger
  // picture before a frame of that size exists, and the copy below writes
  // width * height pixels whatever width_ happens to say.
  const std::size_t required =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (required > kMaximumFramePixels)
    return;
  if (frame_.size() != required) {
    frame_.assign(required, 0xFF000000U);
    if (texture_ != nullptr) {
      SDL_DestroyTexture(texture_);
      texture_ = nullptr;
    }
  }
  width_ = width;
  height_ = height;
  if (data == nullptr || renderer_ == nullptr)
    return;
  const bool wide = pixelFormat_ == RETRO_PIXEL_FORMAT_XRGB8888;
  const auto *source = static_cast<const std::uint8_t *>(data);
  for (unsigned y = 0; y < height; ++y) {
    const std::uint8_t *row = source + static_cast<std::size_t>(y) * pitch;
    std::uint32_t *target = frame_.data() + static_cast<std::size_t>(y) * width;
    if (wide) {
      const auto *pixels = reinterpret_cast<const std::uint32_t *>(row);
      for (unsigned x = 0; x < width; ++x)
        target[x] = pixels[x] | 0xFF000000U;
      continue;
    }
    const auto *pixels = reinterpret_cast<const std::uint16_t *>(row);
    for (unsigned x = 0; x < width; ++x) {
      const std::uint32_t pixel = pixels[x];
      std::uint32_t red = 0;
      std::uint32_t green = 0;
      std::uint32_t blue = 0;
      if (pixelFormat_ == RETRO_PIXEL_FORMAT_RGB565) {
        red = (pixel >> 11) & 0x1FU;
        green = (pixel >> 5) & 0x3FU;
        blue = pixel & 0x1FU;
        green = (green << 2) | (green >> 4);
      } else {
        red = (pixel >> 10) & 0x1FU;
        green = (pixel >> 5) & 0x1FU;
        blue = pixel & 0x1FU;
        green = (green << 3) | (green >> 2);
      }
      red = (red << 3) | (red >> 2);
      blue = (blue << 3) | (blue >> 2);
      target[x] = 0xFF000000U | (red << 16) | (green << 8) | blue;
    }
  }
  if (texture_ == nullptr) {
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width_, height_);
    if (texture_ == nullptr)
      return;
    SDL_SetTextureScaleMode(texture_, SDL_ScaleModeNearest);
  }
  (void)SDL_UpdateTexture(texture_, nullptr, frame_.data(), static_cast<int>(width_ * sizeof(std::uint32_t)));
}

std::size_t LibretroRuntime::onAudio(const std::int16_t *data, std::size_t frames) {
  // Replayed frames are silent: their audio belongs to a timeline that is
  // about to be rolled back, and rewinding while paused would pile it up.
  if (suppressAudio_)
    return frames;
  if (data == nullptr || frames == 0)
    return frames;
  if (muted_ || audioDevice_ == 0)
    return frames;
  if (fastForward_) {
    // Four emulated frames per displayed frame would overflow the buffer, so
    // keep one buffer out of every divisor and let real time drain the queue.
    if (!fastForwardAudio_ || fastForwardFrames_++ % kFastForwardDivisor != 0)
      return frames;
  }
  queueAudio(data, frames);
  return frames;
}

void LibretroRuntime::queueAudio(const std::int16_t *data, std::size_t frames) {
  if (audioDevice_ == 0)
    return;
  const std::size_t samples = frames * 2U;
  std::vector<std::int16_t> scaled(samples);
  if (volume_ == 100) {
    std::copy(data, data + samples, scaled.begin());
  } else {
    for (std::size_t index = 0; index < samples; ++index)
      scaled[index] = static_cast<std::int16_t>((static_cast<int>(data[index]) * volume_) / 100);
  }
  const Uint32 queuedLimit = static_cast<Uint32>(sampleRate_) * 2U * static_cast<Uint32>(sizeof(std::int16_t)) / 4U;
  if (SDL_GetQueuedAudioSize(audioDevice_) >= queuedLimit)
    return;
  if (resampler_ == nullptr) {
    (void)SDL_QueueAudio(audioDevice_, scaled.data(), static_cast<Uint32>(samples * sizeof(std::int16_t)));
    return;
  }
  if (SDL_AudioStreamPut(resampler_, scaled.data(), static_cast<int>(samples * sizeof(std::int16_t))) < 0)
    return;
  std::vector<std::uint8_t> converted(4096);
  int available = 0;
  while ((available = SDL_AudioStreamAvailable(resampler_)) > 0) {
    if (static_cast<std::size_t>(available) > converted.size())
      converted.resize(static_cast<std::size_t>(available));
    const int got = SDL_AudioStreamGet(resampler_, converted.data(), available);
    if (got <= 0)
      break;
    (void)SDL_QueueAudio(audioDevice_, converted.data(), static_cast<Uint32>(got));
  }
}

void LibretroRuntime::discardAudio() {
  if (audioDevice_ != 0)
    SDL_ClearQueuedAudio(audioDevice_);
}

void LibretroRuntime::setPaused(bool paused) noexcept {
  if (!active_ || paused_ == paused)
    return;
  paused_ = paused;
  releaseInput();
  if (audioDevice_ != 0) {
    SDL_ClearQueuedAudio(audioDevice_);
    SDL_PauseAudioDevice(audioDevice_, paused ? 1 : 0);
  }
  if (!paused_)
    nextFrame_ = SDL_GetPerformanceCounter();
}

void LibretroRuntime::reset() {
  if (!active_)
    return;
  core_.reset();
  discardAudio();
  resetPlayers();
  clearRewind();
  nextFrame_ = SDL_GetPerformanceCounter();
}

void LibretroRuntime::powerCycle() {
  if (!active_)
    return;
  // Keep what the cartridge battery recorded, then load the game again so work
  // RAM and mapper registers start from scratch.
  writeSaveRam();
  if (!rebuildGame()) {
    error_ = "Cannot rebuild the game";
    active_ = false;
    return;
  }
  restoreSaveRam();
  discardAudio();
  if (audioDevice_ != 0)
    SDL_PauseAudioDevice(audioDevice_, paused_ ? 1 : 0);
  resetPlayers();
  clearRewind();
  nextFrame_ = SDL_GetPerformanceCounter();
}

void LibretroRuntime::setFastForward(bool enabled) noexcept {
  if (fastForward_ == enabled)
    return;
  fastForward_ = enabled;
  fastForwardFrames_ = 0;
  if (audioDevice_ != 0)
    SDL_ClearQueuedAudio(audioDevice_);
  nextFrame_ = SDL_GetPerformanceCounter();
}

void LibretroRuntime::setSlowMotion(bool enabled) noexcept {
  if (slowMotion_ == enabled)
    return;
  slowMotion_ = enabled;
  nextFrame_ = SDL_GetPerformanceCounter();
}

void LibretroRuntime::update() {
  if (!active_ || paused_)
    return;
  const Uint64 now = SDL_GetPerformanceCounter();
  if (now < nextFrame_)
    return;
  runFrame();
  Uint64 period = framePeriod_;
  if (fastForward_)
    period /= kFastForwardDivisor;
  else if (slowMotion_)
    period *= kSlowMotionFactor;
  nextFrame_ += period;
  if (now > nextFrame_ + period * 2U)
    nextFrame_ = now + period;
}

void LibretroRuntime::stepFrame() {
  // A frame step runs even while paused, and it must not leave audio queued:
  // paused playback never drains the buffer.
  if (!active_)
    return;
  runFrame();
  discardAudio();
  nextFrame_ = SDL_GetPerformanceCounter();
}

void LibretroRuntime::runFrame() {
  // Turbo is a front-end pulse: the core only ever sees ordinary A/B presses.
  turboPulse_ = config::turboPulseOn(frameCount_);
  if (runaheadFrames_ > 0 && core_.serializeSize() > 0) {
    // Runahead, single instance: run the hidden frames with audio and video
    // muted, remember the machine right after the first one, show the frame
    // that comes next, then roll back so the next input lands earlier in the
    // core than the picture it produced.
    runaheadBuffer_.resize(core_.serializeSize());
    suppressVideo_ = true;
    suppressAudio_ = true;
    core_.run();
    const bool stored =
        core_.serialize(runaheadBuffer_.data(), runaheadBuffer_.size());
    for (int frame = 1; frame < runaheadFrames_; ++frame)
      core_.run();
    suppressVideo_ = false;
    suppressAudio_ = false;
    core_.run();
    if (stored)
      (void)core_.unserialize(runaheadBuffer_.data(), runaheadBuffer_.size());
  } else {
    core_.run();
  }
  turboPulse_ = false;
  ++frameCount_;
  // Rewind samples the machine every other frame; runahead already owns the
  // serializer and the two would fight over the same timeline.
  if (rewindEnabled_ && runaheadFrames_ == 0 &&
      frameCount_ % kRewindIntervalFrames == 0)
    captureRewind();
}

void LibretroRuntime::setRewindEnabled(bool enabled) noexcept {
  if (rewindEnabled_ == enabled)
    return;
  rewindEnabled_ = enabled;
  if (!enabled)
    clearRewind();
}

void LibretroRuntime::setRunaheadFrames(int frames) noexcept {
  const int value = std::clamp(frames, 0, kMaximumRunaheadFrames);
  if (runaheadFrames_ == value)
    return;
  runaheadFrames_ = value;
  if (value > 0)
    clearRewind();
}

bool LibretroRuntime::rewindStep() noexcept {
  // Rewind needs the snapshot on screen plus the one before it; with fewer the
  // history is empty and there is nothing to roll back to.
  if (!active_ || !rewindEnabled_ || rewind_.size() < 2)
    return false;
  // The newest snapshot was taken right after the frame on screen, so it
  // describes the machine the player is already looking at. Dropping it first
  // is what makes the very first press move backwards.
  rewindBytes_ -= rewind_.back().data.size();
  rewind_.pop_back();
  if (rewind_.empty())
    return false;
  const Snapshot &snapshot = rewind_.back();
  const bool restored =
      core_.unserialize(snapshot.data.data(), snapshot.data.size());
  rewindBytes_ -= snapshot.data.size();
  rewind_.pop_back();
  if (!restored)
    return false;
  // Run one frame so the picture matches the restored point in time, but keep
  // its audio out of the queue: the player is scrubbing, not listening.
  turboPulse_ = false;
  suppressAudio_ = true;
  core_.run();
  suppressAudio_ = false;
  discardAudio();
  nextFrame_ = SDL_GetPerformanceCounter();
  return true;
}

void LibretroRuntime::clearRewind() {
  rewind_.clear();
  rewindBytes_ = 0;
}

void LibretroRuntime::captureRewind() {
  const std::size_t size = core_.serializeSize();
  if (size == 0 || size > kRewindMaximumBytes)
    return;
  Snapshot snapshot;
  snapshot.data.resize(size);
  if (!core_.serialize(snapshot.data.data(), size))
    return;
  snapshot.frame = frameCount_;
  rewindBytes_ += size;
  rewind_.push_back(std::move(snapshot));
  while (rewind_.size() > kRewindMaximumSnapshots ||
         rewindBytes_ > kRewindMaximumBytes) {
    rewindBytes_ -= rewind_.front().data.size();
    rewind_.pop_front();
  }
}

void LibretroRuntime::setVideoOptions(const VideoOptions &options) noexcept { video_ = options; }

void LibretroRuntime::render(SDL_Renderer *renderer) const {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  if (!active_ || texture_ == nullptr || width_ == 0 || height_ == 0)
    return;
  // The game is fitted inside the 1600x900 canvas, and the canvas is then
  // letterboxed into the real framebuffer; drawing straight to the window would
  // leave the interface and the picture on two different scales.
  int outputWidth = 0;
  int outputHeight = 0;
  SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight);
  const CanvasTransform frame = fitCanvas(outputWidth, outputHeight);
  const Viewport view =
      computeViewport(static_cast<int>(width_), static_cast<int>(height_),
                      kCanvasWidth, kCanvasHeight, video_);
  if (view.sourceWidth <= 0 || view.sourceHeight <= 0)
    return;
  const SDL_Rect source{view.sourceX, view.sourceY, view.sourceWidth,
                        view.sourceHeight};
  const SDL_FRect destination{frame.offsetX + view.x * frame.scale,
                              frame.offsetY + view.y * frame.scale,
                              view.width * frame.scale,
                              view.height * frame.scale};
  SDL_RenderCopyF(renderer, texture_, &source, &destination);
}

std::string LibretroRuntime::statePath(int slot) const {
  if (slot < 0 || slot > 2 || stateDirectory_.empty() || !active_ || gameId_.empty())
    return {};
  const fs::path file =
      paths::fromUtf8(stateDirectory_) /
      paths::fromUtf8(gameId_ + "-slot" + std::to_string(slot + 1) +
                      ".mrgstate");
  return paths::toUtf8(file);
}

bool LibretroRuntime::statePresent(int slot) const noexcept {
  const std::string path = statePath(slot);
  if (path.empty())
    return false;
  std::error_code error;
  return fs::is_regular_file(paths::fromUtf8(path), error) && !error;
}

StateStatus LibretroRuntime::saveState(int slot, std::string &error) {
  error.clear();
  const std::string path = statePath(slot);
  if (path.empty()) {
    error = "Invalid save-state slot";
    return StateStatus::Failed;
  }
  const std::size_t size = core_.serializeSize();
  if (size == 0 || size > kMaximumStateSize) {
    error = "This core cannot save state";
    return StateStatus::Failed;
  }
  stateBuffer_.assign(size, 0);
  if (!core_.serialize(stateBuffer_.data(), stateBuffer_.size())) {
    error = "Cannot capture game state";
    return StateStatus::Failed;
  }
  std::ostringstream header;
  header << kStateMagic << '\n' << name_ << '\n' << stateBuffer_.size() << '\n';
  const std::string head = header.str();
  const std::string temporary = path + ".tmp";
  {
    std::ofstream output(paths::fromUtf8(temporary),
                         std::ios::binary | std::ios::trunc);
    if (output) {
      output.write(head.data(), static_cast<std::streamsize>(head.size()));
      output.write(reinterpret_cast<const char *>(stateBuffer_.data()),
                   static_cast<std::streamsize>(stateBuffer_.size()));
    }
    if (!output) {
      error = "Cannot write save state";
      std::error_code ignored;
      fs::remove(temporary, ignored);
      return StateStatus::Failed;
    }
  }
  const fs::path temporaryPath = paths::fromUtf8(temporary);
  const fs::path statePathValue = paths::fromUtf8(path);
  std::error_code renameError;
  fs::rename(temporaryPath, statePathValue, renameError);
  if (renameError) {
    std::error_code ignored;
    fs::remove(statePathValue, ignored);
    renameError.clear();
    fs::rename(temporaryPath, statePathValue, renameError);
  }
  if (renameError) {
    error = "Cannot finish save state: " + renameError.message();
    std::error_code ignored;
    fs::remove(temporaryPath, ignored);
    return StateStatus::Failed;
  }
  writeStateMetadata(path);
  return StateStatus::Saved;
}

void LibretroRuntime::writeStateMetadata(const std::string &path) const {
  std::ostringstream meta;
  meta << "epoch=" << static_cast<std::uint64_t>(std::time(nullptr)) << '\n' << "frames=" << frameCount_ << '\n';
  writeTextFile(path + ".meta", meta.str());
  if (frame_.empty() || width_ == 0 || height_ == 0)
    return;
  SDL_Surface *thumbnail = SDL_CreateRGBSurfaceWithFormatFrom(
      const_cast<std::uint32_t *>(frame_.data()), static_cast<int>(width_),
      static_cast<int>(height_), 32,
      static_cast<int>(width_ * sizeof(std::uint32_t)), SDL_PIXELFORMAT_ARGB8888);
  if (thumbnail == nullptr)
    return;
  (void)io::saveSurfaceBmp(thumbnail, path + ".bmp");
  SDL_FreeSurface(thumbnail);
}

StateInfo LibretroRuntime::stateInfo(int slot) const {
  StateInfo info;
  const std::string path = statePath(slot);
  if (path.empty())
    return info;
  std::error_code error;
  info.present = fs::is_regular_file(paths::fromUtf8(path), error) && !error;
  if (!info.present)
    return info;
  std::uint64_t epoch = 0;
  std::uint64_t frames = 0;
  std::ifstream meta(paths::fromUtf8(path + ".meta"));
  std::string line;
  while (std::getline(meta, line)) {
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos)
      continue;
    const std::string key = line.substr(0, separator);
    const std::uint64_t value = std::strtoull(line.c_str() + separator + 1, nullptr, 10);
    if (key == "epoch")
      epoch = value;
    else if (key == "frames")
      frames = value;
  }
  if (epoch != 0) {
    const std::time_t stamp = static_cast<std::time_t>(epoch);
    const std::tm *local = std::localtime(&stamp);
    if (local != nullptr) {
      std::ostringstream output;
      output << std::put_time(local, "%Y-%m-%d %H:%M");
      info.savedAt = output.str();
    }
  }
  if (frames != 0) {
    const double rate = fps_ > 0.0 ? fps_ : 60.0;
    const auto total = static_cast<std::uint64_t>(static_cast<double>(frames) / rate);
    std::ostringstream playTime;
    playTime << (total / 3600U) << ':' << std::setw(2) << std::setfill('0') << ((total / 60U) % 60U) << ':'
             << std::setw(2) << std::setfill('0') << (total % 60U);
    info.playTime = playTime.str();
  }
  error.clear();
  if (fs::is_regular_file(paths::fromUtf8(path + ".bmp"), error) && !error)
    info.thumbnailPath = path + ".bmp";
  return info;
}

StateStatus LibretroRuntime::loadState(int slot, std::string &error) {
  error.clear();
  const std::string path = statePath(slot);
  if (path.empty()) {
    error = "Invalid save-state slot";
    return StateStatus::Failed;
  }
  if (!statePresent(slot))
    return StateStatus::Missing;
  std::vector<std::uint8_t> bytes;
  if (!readFile(path, bytes, kMaximumStateSize)) {
    error = "Cannot read save state";
    return StateStatus::Failed;
  }
  const auto lineEnd = [&bytes](std::size_t start) {
    std::size_t end = start;
    while (end < bytes.size() && bytes[end] != '\n')
      ++end;
    return end;
  };
  const std::size_t magicEnd = lineEnd(0);
  if (magicEnd >= bytes.size() || magicEnd != std::strlen(kStateMagic) ||
      std::memcmp(bytes.data(), kStateMagic, magicEnd) != 0) {
    error = "Save state is incompatible or damaged";
    return StateStatus::Failed;
  }
  const std::size_t nameEnd = lineEnd(magicEnd + 1);
  if (nameEnd >= bytes.size()) {
    error = "Save state is incompatible or damaged";
    return StateStatus::Failed;
  }
  const std::size_t sizeEnd = lineEnd(nameEnd + 1);
  if (sizeEnd >= bytes.size()) {
    error = "Save state is incompatible or damaged";
    return StateStatus::Failed;
  }
  const std::string savedName(reinterpret_cast<const char *>(bytes.data() + magicEnd + 1),
                              nameEnd - magicEnd - 1);
  const std::string payloadSize(reinterpret_cast<const char *>(bytes.data() + nameEnd + 1), sizeEnd - nameEnd - 1);
  const std::size_t expected = static_cast<std::size_t>(std::strtoull(payloadSize.c_str(), nullptr, 10));
  const std::size_t offset = sizeEnd + 1;
  // Subtract instead of adding: offset + expected could wrap for a header that
  // claims a payload near the size limit.
  if (expected == 0 || offset >= bytes.size() ||
      expected > bytes.size() - offset) {
    error = "Save state is incompatible or damaged";
    return StateStatus::Failed;
  }
  // The header records the core that wrote the state. Feeding another core's
  // bytes to unserialize is how a save silently corrupts the running game.
  if (savedName != name_) {
    error = savedName.empty() ? "This save state does not name its core"
                              : "This save state belongs to " + savedName;
    return StateStatus::Failed;
  }
  if (!core_.unserialize(bytes.data() + offset, expected)) {
    error = "Save state is incompatible or damaged";
    return StateStatus::Failed;
  }
  releaseInput();
  discardAudio();
  // Snapshots taken before the load belong to a timeline that no longer
  // exists, so the rewind window starts over from here.
  clearRewind();
  nextFrame_ = SDL_GetPerformanceCounter();
  return StateStatus::Loaded;
}

CoreOption *LibretroRuntime::findCoreOption(std::string_view key) {
  for (CoreOption &option : options_) {
    if (option.key.compare(key) == 0)
      return &option;
  }
  return nullptr;
}

bool LibretroRuntime::declareCoreOptionsV1Definitions(const retro_core_option_definition *definitions) {
  if (definitions == nullptr)
    return false;
  options_.clear();
  for (const retro_core_option_definition *definition = definitions; definition->key != nullptr;
       ++definition) {
    CoreOption option;
    option.key = definition->key;
    option.label = definition->desc != nullptr ? definition->desc : definition->key;
    if (definition->info != nullptr)
      option.info = definition->info;
    for (const retro_core_option_value *value = definition->values; value != nullptr && value->value != nullptr;
         ++value) {
      CoreOptionValue entry;
      entry.value = value->value;
      entry.label = value->label != nullptr ? value->label : value->value;
      option.values.push_back(std::move(entry));
    }
    if (option.values.empty())
      continue;
    if (definition->default_value != nullptr) {
      for (std::size_t index = 0; index < option.values.size(); ++index) {
        if (option.values[index].value == definition->default_value) {
          option.defaultSelected = index;
          break;
        }
      }
    }
    option.selected = option.defaultSelected;
    options_.push_back(std::move(option));
  }
  applyStoredCoreOptions();
  return !options_.empty();
}

bool LibretroRuntime::declareCoreOptionsV1(const retro_variable *variables) {
  if (variables == nullptr)
    return false;
  options_.clear();
  for (const retro_variable *variable = variables; variable->key != nullptr; ++variable) {
    CoreOption option;
    option.key = variable->key;
    const std::string description = variable->value != nullptr ? variable->value : "";
    const std::size_t separator = description.find(';');
    if (separator == std::string::npos) {
      // A version 1 entry without a value list is a boolean in disguise.
      option.label = trim(description);
      option.values.push_back({option.label, option.label});
    } else {
      option.label = trim(description.substr(0, separator));
      const std::string rest = description.substr(separator + 1);
      std::size_t start = 0;
      while (start <= rest.size()) {
        const std::size_t end = rest.find('|', start);
        const std::string value =
            trim(rest.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (!value.empty())
          option.values.push_back({value, value});
        if (end == std::string::npos)
          break;
        start = end + 1;
      }
    }
    if (option.key.empty() || option.values.empty())
      continue;
    options_.push_back(std::move(option));
  }
  applyStoredCoreOptions();
  return !options_.empty();
}

bool LibretroRuntime::declareCoreOptionsV2(const retro_core_options_v2 *definitions) {
  if (definitions == nullptr)
    return false;
  std::vector<std::pair<std::string, std::string>> categories;
  for (const retro_core_option_v2_category *category = definitions->categories;
       category != nullptr && category->key != nullptr; ++category) {
    const std::string desc = category->desc != nullptr ? category->desc : category->key;
    categories.emplace_back(category->key, desc);
  }
  options_.clear();
  for (const retro_core_option_v2_definition *definition = definitions->definitions;
       definition != nullptr && definition->key != nullptr; ++definition) {
    CoreOption option;
    option.key = definition->key;
    option.label = definition->desc != nullptr ? definition->desc : definition->key;
    if (definition->info != nullptr)
      option.info = definition->info;
    if (definition->category_key != nullptr) {
      for (const auto &entry : categories) {
        if (entry.first == definition->category_key) {
          option.category = entry.second;
          break;
        }
      }
    }
    for (const retro_core_option_value *value = definition->values;
         value != nullptr && value->value != nullptr; ++value) {
      CoreOptionValue entry;
      entry.value = value->value;
      entry.label = value->label != nullptr ? value->label : value->value;
      option.values.push_back(std::move(entry));
    }
    if (option.values.empty())
      continue;
    option.defaultSelected = 0;
    if (definition->default_value != nullptr) {
      for (std::size_t index = 0; index < option.values.size(); ++index) {
        if (option.values[index].value == definition->default_value) {
          option.defaultSelected = index;
          break;
        }
      }
    }
    option.selected = option.defaultSelected;
    options_.push_back(std::move(option));
  }
  applyStoredCoreOptions();
  return !options_.empty();
}

void LibretroRuntime::applyStoredCoreOptions() {
  for (CoreOption &option : options_) {
    for (const auto &entry : storedOptions_) {
      if (entry.first != option.key)
        continue;
      for (std::size_t index = 0; index < option.values.size(); ++index) {
        if (option.values[index].value == entry.second) {
          option.selected = index;
          break;
        }
      }
      break;
    }
  }
}

void LibretroRuntime::loadCoreOptionValues() {
  storedOptions_.clear();
  if (optionsPath_.empty())
    return;
  std::ifstream input(paths::fromUtf8(optionsPath_));
  if (!input)
    return;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#')
      continue;
    const std::size_t tab = line.find('\t');
    if (tab == std::string::npos)
      continue;
    storedOptions_.emplace_back(line.substr(0, tab), line.substr(tab + 1));
  }
}

void LibretroRuntime::saveCoreOptionValues() const {
  if (optionsPath_.empty())
    return;
  std::string text;
  for (const auto &entry : storedOptions_)
    text += entry.first + "\t" + entry.second + "\n";
  writeTextFile(optionsPath_, text);
}

bool LibretroRuntime::setCoreOption(std::string_view key, std::string_view value) {
  CoreOption *option = findCoreOption(key);
  if (option == nullptr)
    return false;
  for (std::size_t index = 0; index < option->values.size(); ++index) {
    const CoreOptionValue &entry = option->values[index];
    if (entry.value != value)
      continue;
    option->selected = index;
    bool stored = false;
    for (auto &candidate : storedOptions_) {
      if (candidate.first == option->key) {
        candidate.second = entry.value;
        stored = true;
        break;
      }
    }
    if (!stored)
      storedOptions_.emplace_back(option->key, entry.value);
    saveCoreOptionValues();
    optionsChanged_ = true;
    return true;
  }
  return false;
}

void LibretroRuntime::resetCoreOptions() {
  for (CoreOption &option : options_)
    option.selected = option.defaultSelected;
  storedOptions_.clear();
  if (!optionsPath_.empty()) {
    std::error_code ignored;
    fs::remove(paths::fromUtf8(optionsPath_), ignored);
  }
  optionsChanged_ = true;
}

} // namespace launcher::runtime::libretro
