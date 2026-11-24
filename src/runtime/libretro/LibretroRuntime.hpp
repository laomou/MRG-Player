#pragma once

#include <SDL.h>

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/InputBindings.hpp"
#include "game/GamePlatform.hpp"
#include "runtime/IGameRuntime.hpp"
#include "runtime/VideoOptions.hpp"
#include "runtime/libretro/LibretroCore.hpp"

namespace launcher::runtime::libretro {

// Plays one game through an installed libretro core.
//
// The class keeps the same contract as the built-in NES runtime: it owns its
// audio device, paces frames against the performance counter, stores save
// states in the launcher state directory with a thumbnail, and keeps battery
// saves under saves/<platform>/.
class LibretroRuntime final : public IGameRuntime {
public:
  LibretroRuntime();
  ~LibretroRuntime() override;
  LibretroRuntime(const LibretroRuntime &) = delete;
  LibretroRuntime &operator=(const LibretroRuntime &) = delete;

  bool load(const library::GameEntry &game, SDL_Renderer *renderer, int volume) override;
  void stop() override;
  void handleEvent(const SDL_Event &event) override;
  void releaseInput() noexcept override;
  void update() override;
  void render(SDL_Renderer *renderer) const override;
  void setVolume(int volume) noexcept override;
  void setMuted(bool muted) noexcept override;
  void setFastForwardAudio(bool keep) noexcept override;
  void setInputBindings(const config::InputBindings &bindings) override;
  void setPaused(bool paused) noexcept override;
  void reset() override;
  void powerCycle() override;
  void setFastForward(bool enabled) noexcept override;
  void setSlowMotion(bool enabled) noexcept override;
  void stepFrame() override;
  void setRewindEnabled(bool enabled) noexcept override;
  [[nodiscard]] bool rewindStep() noexcept override;
  void setRunaheadFrames(int frames) noexcept override;
  void setVideoOptions(const VideoOptions &options) noexcept override;
  void setPlayerController(int player, SDL_JoystickID instanceId) noexcept override;
  StateStatus saveState(int slot, std::string &error) override;
  StateStatus loadState(int slot, std::string &error) override;
  [[nodiscard]] bool statePresent(int slot) const noexcept override;
  [[nodiscard]] StateInfo stateInfo(int slot) const override;

  [[nodiscard]] const CoreOptionList &coreOptions() const noexcept override {
    return options_;
  }
  bool setCoreOption(std::string_view key, std::string_view value) override;
  void resetCoreOptions() override;

  [[nodiscard]] game::GamePlatform platform() const noexcept override { return platform_; }
  [[nodiscard]] std::string_view name() const noexcept override { return name_; }
  [[nodiscard]] std::string_view error() const noexcept override { return error_; }

private:
  struct AxisState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
  };

  // Environment callback: what the core asks the front end for.
  bool onEnvironment(unsigned command, void *data);
  // Frame callback: converts the core pixel format into the texture buffer.
  void onVideo(const void *data, unsigned width, unsigned height, std::size_t pitch);
  // Audio callbacks, in both the single-sample and the batched form.
  std::size_t onAudio(const std::int16_t *data, std::size_t frames);
  std::int16_t onInput(unsigned port, unsigned device, unsigned index, unsigned id);

  void openAudio();
  void closeAudio();
  void queueAudio(const std::int16_t *data, std::size_t frames);
  void discardAudio();
  void runFrame();
  void ensureTexture(SDL_Renderer *renderer);
  void restoreSaveRam();
  void writeSaveRam() const;
  std::uint16_t joypadMask(int player) const noexcept;

  void resetPlayers() noexcept;
  void setAction(int player, config::InputAction action, bool pressed) noexcept;
  void setControllerAxis(int player, SDL_GameControllerAxis axis, Sint16 value);
  [[nodiscard]] int playerForController(SDL_JoystickID instanceId) const noexcept;

  std::string statePath(int slot) const;
  void writeStateMetadata(const std::string &path) const;
  bool rebuildGame();

  // Rewind holds the machine snapshots themselves, not pictures: the core is
  // rolled back with the same serializer the save slots use.
  struct Snapshot {
    std::vector<std::uint8_t> data;
    std::uint64_t frame = 0;
  };
  void clearRewind();
  void captureRewind();

  // Core options arrive either as the version 1 key/description pairs or as
  // the version 2 definitions with labels and categories. Both are folded into
  // the same list, and stored values are re-applied after every declaration.
  bool declareCoreOptionsV1(const retro_variable *variables);
  bool declareCoreOptionsV1Definitions(const retro_core_option_definition *definitions);
  bool declareCoreOptionsV2(const retro_core_options_v2 *definitions);
  void applyStoredCoreOptions();
  void loadCoreOptionValues();
  void saveCoreOptionValues() const;
  [[nodiscard]] CoreOption *findCoreOption(std::string_view key);

  LibretroCore core_;
  SDL_Renderer *renderer_ = nullptr;
  SDL_Texture *texture_ = nullptr;
  SDL_AudioDeviceID audioDevice_ = 0;
  SDL_AudioStream *resampler_ = nullptr;
  std::string name_;
  std::string error_;
  std::string gameId_;
  std::string gamePath_;
  // The path the core is handed, plus the staged ASCII copy used instead when
  // the original name is one the core cannot open.
  std::string romLoadPath_;
  std::string stagedRomPath_;
  std::string savePath_;
  std::string stateDirectory_;
  std::string systemDirectory_;
  std::string saveDirectory_;
  std::vector<std::uint8_t> romBytes_;
  std::vector<std::uint32_t> frame_;
  std::vector<std::uint8_t> stateBuffer_;
  std::vector<std::uint8_t> runaheadBuffer_;
  std::deque<Snapshot> rewind_;
  std::size_t rewindBytes_ = 0;
  CoreOptionList options_;
  std::string optionsPath_;
  std::vector<std::pair<std::string, std::string>> storedOptions_;
  bool optionsChanged_ = false;

  game::GamePlatform platform_ = game::GamePlatform::Unknown;
  config::InputBindings bindings_{};
  std::array<std::uint8_t, config::kPlayerCount> inputMask_{};
  std::array<AxisState, config::kPlayerCount> axes_{};
  std::array<SDL_JoystickID, config::kPlayerCount> controllerIds_{};
  VideoOptions video_{};

  unsigned width_ = 0;
  unsigned height_ = 0;
  unsigned pixelFormat_ = 0;
  double fps_ = 60.0;
  int sampleRate_ = 44100;
  int deviceRate_ = 44100;
  int volume_ = 100;
  bool muted_ = false;
  bool fastForward_ = false;
  bool fastForwardAudio_ = false;
  bool slowMotion_ = false;
  bool paused_ = false;
  bool active_ = false;
  bool gameLoaded_ = false;
  bool turboPulse_ = false;
  bool rewindEnabled_ = false;
  bool suppressVideo_ = false;
  bool suppressAudio_ = false;
  int runaheadFrames_ = 0;
  std::uint64_t frameCount_ = 0;
  Uint32 fastForwardFrames_ = 0;
  Uint64 framePeriod_ = 0;
  Uint64 nextFrame_ = 0;
};

} // namespace launcher::runtime::libretro
