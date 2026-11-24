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

namespace launcher::runtime::libretro {
namespace {
namespace fs = std::filesystem;

constexpr const char *kStateMagic = "MRGLIBRETRO1";
constexpr std::size_t kMaximumRomSize = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumSaveRamSize = 1024U * 1024U;
constexpr std::size_t kMaximumStateSize = 64U * 1024U * 1024U;
constexpr std::uint32_t kFastForwardDivisor = 4;
constexpr std::uint32_t kSlowMotionFactor = 2;
constexpr int kAudioBufferSamples = 1024;

// Console action order of config::InputAction, mapped onto libretro joypad ids.
constexpr std::array<unsigned, config::kCoreActionCount> kLibretroButtons{
    RETRO_DEVICE_ID_JOYPAD_A,      RETRO_DEVICE_ID_JOYPAD_B,    RETRO_DEVICE_ID_JOYPAD_SELECT,
    RETRO_DEVICE_ID_JOYPAD_START,  RETRO_DEVICE_ID_JOYPAD_UP,   RETRO_DEVICE_ID_JOYPAD_DOWN,
    RETRO_DEVICE_ID_JOYPAD_LEFT,   RETRO_DEVICE_ID_JOYPAD_RIGHT};

bool readFile(const std::string &path, std::vector<std::uint8_t> &bytes, std::size_t limit = kMaximumRomSize) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    return false;
  const std::streamoff size = input.tellg();
  if (size <= 0 || static_cast<std::uint64_t>(size) > limit)
    return false;
  bytes.resize(static_cast<std::size_t>(size));
  input.seekg(0);
  return static_cast<bool>(input.read(reinterpret_cast<char *>(bytes.data()), size));
}

void writeTextFile(const std::string &path, const std::string &text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
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
  platform_ = game.platform;
  gameId_ = game.id;
  gamePath_ = game.filePath;
  renderer_ = renderer;
  const std::string directory = std::string(game::platformDescriptor(game.platform).managedDirectory);
  savePath_ = (fs::path(config::dataSubdirectory("saves", directory)) / (game.id + ".srm")).string();
  stateDirectory_ = config::dataSubdirectory("states", directory);
  systemDirectory_ = (fs::path(config::dataRoot()) / "system").string();
  {
    std::error_code ignored;
    fs::create_directories(systemDirectory_, ignored);
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
  const bool fullPath = core_.needsFullPath();
  if (!core_.loadGame(game.filePath.c_str(), fullPath ? nullptr : romBytes_.data(), fullPath ? 0 : romBytes_.size())) {
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
  gameId_.clear();
  gamePath_.clear();
  savePath_.clear();
  stateDirectory_.clear();
  systemDirectory_.clear();
  saveDirectory_.clear();
  romBytes_.clear();
  frame_.clear();
  stateBuffer_.clear();
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
  if (!core_.loadGame(gamePath_.c_str(), fullPath ? nullptr : romBytes_.data(), fullPath ? 0 : romBytes_.size()))
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
  std::ifstream input(savePath_, std::ios::binary | std::ios::ate);
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
  std::ofstream output(savePath_, std::ios::binary | std::ios::trunc);
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
  const std::uint8_t bit = static_cast<std::uint8_t>(1U << index);
  std::uint8_t &mask = inputMask_[static_cast<std::size_t>(player)];
  if (pressed)
    mask |= bit;
  else
    mask &= static_cast<std::uint8_t>(~bit);
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
  std::uint16_t mask = 0;
  for (std::size_t index = 0; index < config::kCoreActionCount; ++index) {
    if ((inputMask_[port] & static_cast<std::uint8_t>(1U << index)) != 0)
      mask = static_cast<std::uint16_t>(mask | (1U << kLibretroButtons[index]));
  }
  const std::uint8_t held = inputMask_[port];
  const auto holds = [held](config::InputAction action) noexcept {
    return (held & static_cast<std::uint8_t>(1U << static_cast<std::size_t>(action))) != 0;
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
      *static_cast<bool *>(data) = false;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE:
      // No core options are offered, so every core keeps its defaults.
      return false;
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
    case RETRO_ENVIRONMENT_SET_VARIABLES:
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
  if (width == 0 || height == 0)
    return;
  if (width != width_ || height != height_ || frame_.empty()) {
    width_ = width;
    height_ = height;
    frame_.assign(static_cast<std::size_t>(width) * height, 0xFF000000U);
    if (texture_ != nullptr) {
      SDL_DestroyTexture(texture_);
      texture_ = nullptr;
    }
  }
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
  core_.run();
  turboPulse_ = false;
  ++frameCount_;
}

void LibretroRuntime::setVideoOptions(const VideoOptions &options) noexcept { video_ = options; }

void LibretroRuntime::render(SDL_Renderer *renderer) const {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  if (!active_ || texture_ == nullptr || width_ == 0 || height_ == 0)
    return;
  int windowWidth = 0;
  int windowHeight = 0;
  SDL_RenderGetLogicalSize(renderer, &windowWidth, &windowHeight);
  if (windowWidth <= 0 || windowHeight <= 0)
    SDL_GetRendererOutputSize(renderer, &windowWidth, &windowHeight);
  const Viewport view = computeViewport(static_cast<int>(width_), static_cast<int>(height_), windowWidth, windowHeight,
                                        video_);
  if (view.sourceWidth <= 0 || view.sourceHeight <= 0)
    return;
  const SDL_Rect source{view.sourceX, view.sourceY, view.sourceWidth, view.sourceHeight};
  const SDL_FRect destination{view.x, view.y, view.width, view.height};
  SDL_RenderCopyF(renderer, texture_, &source, &destination);
}

std::string LibretroRuntime::statePath(int slot) const {
  if (slot < 0 || slot > 2 || stateDirectory_.empty() || !active_ || gameId_.empty())
    return {};
  return (fs::path(stateDirectory_) / (gameId_ + "-slot" + std::to_string(slot + 1) + ".mrgstate")).string();
}

bool LibretroRuntime::statePresent(int slot) const noexcept {
  const std::string path = statePath(slot);
  if (path.empty())
    return false;
  std::error_code error;
  return fs::is_regular_file(path, error) && !error;
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
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
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
  std::error_code renameError;
  fs::rename(temporary, path, renameError);
  if (renameError) {
    std::error_code ignored;
    fs::remove(path, ignored);
    renameError.clear();
    fs::rename(temporary, path, renameError);
  }
  if (renameError) {
    error = "Cannot finish save state: " + renameError.message();
    std::error_code ignored;
    fs::remove(temporary, ignored);
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
  (void)SDL_SaveBMP(thumbnail, (path + ".bmp").c_str());
  SDL_FreeSurface(thumbnail);
}

StateInfo LibretroRuntime::stateInfo(int slot) const {
  StateInfo info;
  const std::string path = statePath(slot);
  if (path.empty())
    return info;
  std::error_code error;
  info.present = fs::is_regular_file(path, error) && !error;
  if (!info.present)
    return info;
  std::uint64_t epoch = 0;
  std::uint64_t frames = 0;
  std::ifstream meta(path + ".meta");
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
  if (fs::is_regular_file(path + ".bmp", error) && !error)
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
  const std::string payloadSize(reinterpret_cast<const char *>(bytes.data() + nameEnd + 1), sizeEnd - nameEnd - 1);
  const std::size_t expected = static_cast<std::size_t>(std::strtoull(payloadSize.c_str(), nullptr, 10));
  const std::size_t offset = sizeEnd + 1;
  if (expected == 0 || offset + expected > bytes.size()) {
    error = "Save state is incompatible or damaged";
    return StateStatus::Failed;
  }
  if (!core_.unserialize(bytes.data() + offset, expected)) {
    error = "Save state is incompatible or damaged";
    return StateStatus::Failed;
  }
  releaseInput();
  discardAudio();
  nextFrame_ = SDL_GetPerformanceCounter();
  return StateStatus::Loaded;
}

} // namespace launcher::runtime::libretro
