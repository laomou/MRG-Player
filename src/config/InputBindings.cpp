#include "config/InputBindings.hpp"

#include <algorithm>
#include <cctype>

namespace launcher::config {
namespace {

using KeyMap = std::array<SDL_Keycode, kInputActionCount>;

// What a port with no keyboard reads from: nothing. SDL reports an unbound key
// as SDLK_UNKNOWN, which no event ever carries, so the table never matches. A
// write lands here and is forgotten, which is what the settings page deserves
// for asking a pad-only port to bind a key.
KeyMap kNoKeys{};

} // namespace

InputBindings defaultInputBindings() {
  InputBindings bindings;
  // The conventional RetroArch pair — X/Z for the face buttons, the arrow keys
  // for the d-pad — with the upper pair and the shoulders on S/A and Q/W.
  // Turbo remains on C/V because it is an MRG-only action rather than a console
  // button.
  bindings.keyboard = {SDLK_x,    SDLK_z,    SDLK_RSHIFT, SDLK_RETURN,
                       SDLK_UP,   SDLK_DOWN, SDLK_LEFT,   SDLK_RIGHT,
                       SDLK_c,    SDLK_v,    SDLK_s,      SDLK_a,
                       SDLK_q,    SDLK_w};
  // Six face buttons plus two shoulders is what a Super Nintendo or Genesis
  // pad asks for, so the turbo pulses moved off the face buttons — a pulse
  // there would press the real Y or B every other frame — and onto the two
  // stick clicks, which no console action uses.
  bindings.controller = {
      SDL_CONTROLLER_BUTTON_A,            SDL_CONTROLLER_BUTTON_X,
      SDL_CONTROLLER_BUTTON_BACK,         SDL_CONTROLLER_BUTTON_START,
      SDL_CONTROLLER_BUTTON_DPAD_UP,      SDL_CONTROLLER_BUTTON_DPAD_DOWN,
      SDL_CONTROLLER_BUTTON_DPAD_LEFT,    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
      SDL_CONTROLLER_BUTTON_LEFTSTICK,    SDL_CONTROLLER_BUTTON_RIGHTSTICK,
      SDL_CONTROLLER_BUTTON_Y,            SDL_CONTROLLER_BUTTON_B,
      SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
  // A second pad uses the same buttons as the first, so player 2 shares the pad
  // layout; there is no second keyboard layout to fill in.
  bindings.controllerPlayer2 = bindings.controller;
  bindings.analogDeadZone = 16000;
  return bindings;
}

SDL_Keycode launcherMenuKey(const InputBindings &bindings,
                            SDL_Keycode key) noexcept {
  const auto &keys = bindings.keyboard;
  const auto bound = [&](InputAction action) {
    return keys[static_cast<std::size_t>(action)];
  };
  // Confirmation, and the button a pad player reaches for to start a game, both
  // land on Enter -- the menu's own confirm key. Back is Escape so that leaving
  // the shelf asks before it quits, exactly as the pad's B does on a desktop.
  if (key == bound(InputAction::A) || key == bound(InputAction::Start))
    return SDLK_RETURN;
  if (key == bound(InputAction::B))
    return SDLK_ESCAPE;
  // X opens the drawer and Y does the same, matching the pad.
  if (key == bound(InputAction::X) || key == bound(InputAction::Y))
    return SDLK_x;
  if (key == bound(InputAction::Up))
    return SDLK_UP;
  if (key == bound(InputAction::Down))
    return SDLK_DOWN;
  if (key == bound(InputAction::Left))
    return SDLK_LEFT;
  if (key == bound(InputAction::Right))
    return SDLK_RIGHT;
  return key;
}

std::array<SDL_Keycode, kInputActionCount> &
keyboardBindings(InputBindings &bindings, std::size_t player) noexcept {
  return player == 0 ? bindings.keyboard : kNoKeys;
}

const std::array<SDL_Keycode, kInputActionCount> &
keyboardBindings(const InputBindings &bindings, std::size_t player) noexcept {
  return player == 0 ? bindings.keyboard : kNoKeys;
}

std::array<SDL_GameControllerButton, kInputActionCount> &
controllerBindings(InputBindings &bindings, std::size_t player) noexcept {
  return player == 0 ? bindings.controller : bindings.controllerPlayer2;
}

const std::array<SDL_GameControllerButton, kInputActionCount> &
controllerBindings(const InputBindings &bindings, std::size_t player) noexcept {
  return player == 0 ? bindings.controller : bindings.controllerPlayer2;
}

bool turboPulseOn(std::uint64_t frame) noexcept {
  return (frame / kTurboHalfFrames) % 2U == 0;
}

const char *inputActionName(InputAction action) noexcept {
  static constexpr const char *kNames[kInputActionCount] = {
      "A",     "B",     "SELECT",  "START",  "UP",      "DOWN",   "LEFT",
      "RIGHT", "TURBO A", "TURBO B", "X",    "Y",       "L",      "R"};
  const std::size_t index = static_cast<std::size_t>(action);
  return index < kInputActionCount ? kNames[index] : "?";
}

std::string keyBindingName(SDL_Keycode key) {
  const char *name = SDL_GetKeyName(key);
  return name != nullptr && *name != '\0' ? name : "?";
}

std::string controllerBindingName(SDL_GameControllerButton button) {
  const char *name = SDL_GameControllerGetStringForButton(button);
  if (name == nullptr || *name == '\0')
    return "?";
  std::string value(name);
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

ControllerStyle
resolveControllerStyle(ControllerStyle configured,
                       SDL_GameControllerType detected) noexcept {
  if (configured != ControllerStyle::Auto)
    return configured;
  switch (detected) {
  case SDL_CONTROLLER_TYPE_XBOX360:
  case SDL_CONTROLLER_TYPE_XBOXONE:
    return ControllerStyle::Xbox;
  case SDL_CONTROLLER_TYPE_PS3:
  case SDL_CONTROLLER_TYPE_PS4:
  case SDL_CONTROLLER_TYPE_PS5:
    return ControllerStyle::PlayStation;
  case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
  case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
  case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
  case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    return ControllerStyle::Nintendo;
  default:
    return ControllerStyle::Generic;
  }
}

InputDeviceKind resolvePromptDevice(PromptDeviceMode mode,
                                    InputDeviceKind lastUsed,
                                    bool controllerConnected) noexcept {
  if (mode == PromptDeviceMode::KeyboardMouse)
    return InputDeviceKind::KeyboardMouse;
  if (mode == PromptDeviceMode::Controller)
    return InputDeviceKind::Controller;
  if (lastUsed == InputDeviceKind::Controller && !controllerConnected)
    return InputDeviceKind::KeyboardMouse;
  return lastUsed;
}

std::string controllerButtonPrompt(SDL_GameControllerButton button,
                                   ControllerStyle style) {
  if (style == ControllerStyle::Nintendo) {
    if (button == SDL_CONTROLLER_BUTTON_A)
      return "B";
    if (button == SDL_CONTROLLER_BUTTON_B)
      return "A";
    if (button == SDL_CONTROLLER_BUTTON_X)
      return "Y";
    if (button == SDL_CONTROLLER_BUTTON_Y)
      return "X";
  } else if (style == ControllerStyle::PlayStation) {
    if (button == SDL_CONTROLLER_BUTTON_A)
      return "CROSS";
    if (button == SDL_CONTROLLER_BUTTON_B)
      return "CIRCLE";
    if (button == SDL_CONTROLLER_BUTTON_X)
      return "SQUARE";
    if (button == SDL_CONTROLLER_BUTTON_Y)
      return "TRIANGLE";
  }
  return controllerBindingName(button);
}

InputPrompts inputPrompts(InputDeviceKind device, ControllerStyle style,
                          const InputBindings *bindings) {
  static const InputBindings fallback = defaultInputBindings();
  const InputBindings &bound = bindings != nullptr ? *bindings : fallback;
  const std::size_t options = static_cast<std::size_t>(InputAction::X);
  if (device == InputDeviceKind::KeyboardMouse)
    return {"ENTER",
            "ESC",
            "TAB",
            "ENTER",
            "LEFT/RIGHT",
            ".",
            keyBindingName(bound.keyboard[options]),
            PromptSource::keyboard()};
  std::string category = "LB/RB";
  if (style == ControllerStyle::Nintendo)
    category = "L/R";
  else if (style == ControllerStyle::PlayStation)
    category = "L1/R1";
  return {controllerButtonPrompt(SDL_CONTROLLER_BUTTON_A, style),
          controllerButtonPrompt(SDL_CONTROLLER_BUTTON_B, style),
          category,
          controllerButtonPrompt(SDL_CONTROLLER_BUTTON_A, style),
          "D-PAD",
          "RB",
          // The launcher's drawer answers to the button printed X, whatever
          // the binding does inside a game.
          controllerButtonPrompt(SDL_CONTROLLER_BUTTON_X, style),
          PromptSource::controller()};
}

} // namespace launcher::config
