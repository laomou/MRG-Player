#pragma once

#include <SDL.h>

#include <string>
#include <string_view>

#include "config/InputBindings.hpp"
#include "game/GamePlatform.hpp"
#include "runtime/CoreOptions.hpp"
#include "runtime/VideoOptions.hpp"

namespace launcher::library {
struct GameEntry;
}

namespace launcher::runtime {

enum class StateStatus { Saved, Loaded, Missing, Failed, Unsupported };

// Description of one save-state slot so the pause menu can show when the
// snapshot was taken and what it looked like.
struct StateInfo {
  bool present = false;
  std::string savedAt;
  std::string playTime;
  std::string thumbnailPath;
};

class IGameRuntime {
public:
  virtual ~IGameRuntime() = default;

  virtual bool load(const library::GameEntry &game, SDL_Renderer *renderer,
                    int volume) = 0;
  virtual void stop() = 0;
  virtual void handleEvent(const SDL_Event &event) = 0;
  virtual void releaseInput() noexcept = 0;
  virtual void update() = 0;
  virtual void render(SDL_Renderer *renderer) const = 0;
  virtual void setVolume(int volume) noexcept = 0;
  // Silences the output without touching the saved volume, so a mute toggle
  // can be flipped back to the exact level the player configured.
  virtual void setMuted(bool muted) noexcept { (void)muted; }
  // Chooses what fast-forward does with audio: silence it (drop the samples)
  // or keep it audible by dropping whole sample buffers.
  virtual void setFastForwardAudio(bool keep) noexcept { (void)keep; }
  virtual void setInputBindings(const config::InputBindings &bindings) = 0;
  virtual void setPaused(bool paused) noexcept = 0;
  // Console controls. Reset restarts the CPU while work RAM and mapper
  // registers survive, exactly like the front-panel button; a power cycle
  // rebuilds the machine from the cartridge and reloads battery saves.
  virtual void reset() {}
  virtual void powerCycle() {}
  // Fast-forward runs the console ahead of the wall clock. Runtimes that do
  // not pace their frames can ignore it.
  virtual void setFastForward(bool enabled) noexcept { (void)enabled; }
  // Slow motion halves the emulated speed; a frame step advances exactly one
  // frame, so the pause menu can scrub the picture without resuming.
  virtual void setSlowMotion(bool enabled) noexcept { (void)enabled; }
  virtual void stepFrame() {}
  // Rewind keeps a rolling window of machine snapshots while the player plays
  // and walks back through them on demand. Runtimes that cannot snapshot the
  // machine ignore the request, and rewindStep() then reports false.
  virtual void setRewindEnabled(bool enabled) noexcept { (void)enabled; }
  [[nodiscard]] virtual bool rewindStep() noexcept { return false; }
  // Runahead hides one or more frames between the input and the picture: the
  // frame on screen comes from an older input state, so the game reacts
  // sooner. Zero disables it.
  virtual void setRunaheadFrames(int frames) noexcept { (void)frames; }
  virtual void setVideoOptions(const VideoOptions &options) noexcept {
    (void)options;
  }
  // Binds an SDL controller instance to a console port (0 or 1). Runtimes that
  // only expose one port may ignore the second assignment.
  virtual void setPlayerController(int player,
                                   SDL_JoystickID instanceId) noexcept {
    (void)player;
    (void)instanceId;
  }
  // Settings a core declares for itself. Runtimes without such settings
  // report an empty list, which hides the pause-menu panel.
  [[nodiscard]] virtual const CoreOptionList &coreOptions() const noexcept {
    static const CoreOptionList empty;
    return empty;
  }
  virtual bool setCoreOption(std::string_view key, std::string_view value) {
    (void)key;
    (void)value;
    return false;
  }
  // Drops the saved values and returns every option to the core's default.
  virtual void resetCoreOptions() {}

  virtual StateStatus saveState(int slot, std::string &error) = 0;
  virtual StateStatus loadState(int slot, std::string &error) = 0;
  [[nodiscard]] virtual bool statePresent(int slot) const noexcept = 0;
  [[nodiscard]] virtual StateInfo stateInfo(int slot) const {
    (void)slot;
    return {};
  }

  [[nodiscard]] virtual game::GamePlatform platform() const noexcept = 0;
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;
  [[nodiscard]] virtual std::string_view error() const noexcept = 0;
};

} // namespace launcher::runtime
