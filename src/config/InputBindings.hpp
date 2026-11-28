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
  // The buttons only the wider pads have: the extra face pair of the SNES and
  // the shoulder pair of the SNES, the GBA and the Genesis. They stay at the
  // end of the enum because the settings file is index-aligned with it; a new
  // action appended here never moves a stored binding to another action.
  X,
  Y,
  L,
  R,
  Count
};

inline constexpr std::size_t kInputActionCount =
    static_cast<std::size_t>(InputAction::Count);

// Every action is one bit of the held-input mask, which is sixteen bits wide to
// match the libretro joypad id space.
static_assert(kInputActionCount <= 16,
              "the held-input mask holds one bit per action");

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

// The console has two controller ports. Both take a pad; only the first has a
// keyboard, because two people on one keyboard end up sharing the keys they
// need -- a second player is a second pad.
inline constexpr std::size_t kPlayerCount = 2;

struct InputBindings {
  std::array<SDL_Keycode, kInputActionCount> keyboard{};
  std::array<SDL_GameControllerButton, kInputActionCount> controller{};
  std::array<SDL_GameControllerButton, kInputActionCount> controllerPlayer2{};
  int analogDeadZone = 16000;
};

// The launcher's own menus answer to the pad's face buttons, so a key bound to
// one of them has to mean the same thing there: the shelf cannot ask a player to
// learn that confirmation is Enter after the games taught them it is A. Keys
// bound to A, B, X, Y, START and the d-pad are rewritten into the keys those
// menus already understand; anything else is returned untouched.
[[nodiscard]] SDL_Keycode launcherMenuKey(const InputBindings &bindings,
                                          SDL_Keycode key) noexcept;

// Player 1's key table. Ports past the first answer to a pad only, so they get
// an empty table: every entry is SDLK_UNKNOWN, which no key event carries.
[[nodiscard]] std::array<SDL_Keycode, kInputActionCount> &
keyboardBindings(InputBindings &bindings, std::size_t player) noexcept;
[[nodiscard]] const std::array<SDL_Keycode, kInputActionCount> &
keyboardBindings(const InputBindings &bindings, std::size_t player) noexcept;
[[nodiscard]] std::array<SDL_GameControllerButton, kInputActionCount> &
controllerBindings(InputBindings &bindings, std::size_t player) noexcept;
[[nodiscard]] const std::array<SDL_GameControllerButton, kInputActionCount> &
controllerBindings(const InputBindings &bindings, std::size_t player) noexcept;

// Which device the prompt strings speak for. A couple of shortcuts answer to a
// keyboard key and nothing else -- resetting the bindings, and holding the
// rewind key -- so the screens that would write those keys down have to know
// who is holding the device.
//
// The wrapper earns its keep by having no default constructor: a prompt set that
// forgets to name its device does not compile, where a plain "bool keyboard =
// false" member is quietly perfect at birth and would advertise keys the player
// cannot press.
class PromptSource {
public:
  [[nodiscard]] static constexpr PromptSource keyboard() noexcept {
    return PromptSource(true);
  }
  [[nodiscard]] static constexpr PromptSource controller() noexcept {
    return PromptSource(false);
  }
  PromptSource() = delete;

  [[nodiscard]] constexpr bool fromKeyboard() const noexcept {
    return fromKeyboard_;
  }
  [[nodiscard]] constexpr bool fromController() const noexcept {
    return !fromKeyboard_;
  }

private:
  constexpr explicit PromptSource(bool fromKeyboard) noexcept
      : fromKeyboard_(fromKeyboard) {}

  bool fromKeyboard_;
};

struct InputPrompts {
  std::string confirm;
  std::string back;
  std::string category;
  std::string change;
  std::string horizontal;
  std::string frameStep;
  // The button that opens a game's options drawer on the shelf. It follows the
  // bindings, so the strip names the key the player actually presses.
  std::string options;
  PromptSource source;
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
// Short names for the controller page: it prints fourteen rows side by side on
// a 1024 canvas, so the value half has room for a key and a button but not for
// "RIGHTSHOULDER" next to it. Only the dense list uses these; every other screen
// keeps the full printable name.
[[nodiscard]] std::string keyBindingShortName(SDL_Keycode key);
[[nodiscard]] std::string
controllerButtonShortPrompt(SDL_GameControllerButton button,
                             ControllerStyle style);
// Bindings decide the words for buttons whose bound key is not a menu key --
// the shelf's options drawer -- so the prompt set has to see them. Passing none
// falls back to the defaults, which is what the tests and the first frame use.
InputPrompts inputPrompts(InputDeviceKind device, ControllerStyle style,
                          const InputBindings *bindings = nullptr);

} // namespace launcher::config
