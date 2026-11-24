#pragma once

#include <SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace launcher::config {

enum class InputAction : std::size_t {
  A = 0,
  B,
  Select,
  Start,
  Up,
  Down,
  Left,
  Right,
  // Turbo A/B are not console buttons: while they are held the matching face
  // button is pulsed by the runtime, so games see a burst of fast presses.
  TurboA,
  TurboB,
  Count
};

inline constexpr std::size_t kInputActionCount =
    static_cast<std::size_t>(InputAction::Count);

// The actions that map one-to-one onto a console controller. Everything from
// here on is front-end behaviour (turbo) and never reaches the core directly.
inline constexpr std::size_t kCoreActionCount = 8;
static_assert(kCoreActionCount <= kInputActionCount);

// Turbo keeps the button pressed for kTurboHalfFrames frames and released for
// the next kTurboHalfFrames. Two frames each way is 15 presses per second on an
// NTSC field rate, which is the rate arcade hardware used.
inline constexpr std::uint64_t kTurboHalfFrames = 2;

// True while the turbo pulse is in its pressed half of the cycle.
[[nodiscard]] bool turboPulseOn(std::uint64_t frame) noexcept;

enum class PromptDeviceMode : int {
  Auto = 0,
  KeyboardMouse = 1,
  Controller = 2
};
enum class InputDeviceKind : int { KeyboardMouse = 0, Controller = 1 };
enum class ControllerStyle : int {
  Auto = 0,
  Xbox = 1,
  Nintendo = 2,
  PlayStation = 3,
  Generic = 4
};

// The console has two controller ports; every binding set is kept per player
// so two people can share one keyboard and one pad.
inline constexpr std::size_t kPlayerCount = 2;

struct InputBindings {
  std::array<SDL_Keycode, kInputActionCount> keyboard{};
  std::array<SDL_GameControllerButton, kInputActionCount> controller{};
  std::array<SDL_Keycode, kInputActionCount> keyboardPlayer2{};
  std::array<SDL_GameControllerButton, kInputActionCount> controllerPlayer2{};
  int analogDeadZone = 16000;
};

[[nodiscard]] std::array<SDL_Keycode, kInputActionCount> &
keyboardBindings(InputBindings &bindings, std::size_t player) noexcept;
[[nodiscard]] const std::array<SDL_Keycode, kInputActionCount> &
keyboardBindings(const InputBindings &bindings, std::size_t player) noexcept;
[[nodiscard]] std::array<SDL_GameControllerButton, kInputActionCount> &
controllerBindings(InputBindings &bindings, std::size_t player) noexcept;
[[nodiscard]] const std::array<SDL_GameControllerButton, kInputActionCount> &
controllerBindings(const InputBindings &bindings, std::size_t player) noexcept;

struct InputPrompts {
  std::string confirm;
  std::string manage;
  std::string back;
  std::string category;
  std::string change;
  std::string horizontal;
  std::string frameStep;
};

InputBindings defaultInputBindings();
const char *inputActionName(InputAction action) noexcept;
std::string keyBindingName(SDL_Keycode key);
std::string controllerBindingName(SDL_GameControllerButton button);
ControllerStyle
resolveControllerStyle(ControllerStyle configured,
                       SDL_GameControllerType detected) noexcept;
InputDeviceKind resolvePromptDevice(PromptDeviceMode mode,
                                    InputDeviceKind lastUsed,
                                    bool controllerConnected) noexcept;
std::string controllerButtonPrompt(SDL_GameControllerButton button,
                                   ControllerStyle style);
InputPrompts inputPrompts(InputDeviceKind device, ControllerStyle style);

} // namespace launcher::config
