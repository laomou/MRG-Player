#include "config/InputBindings.hpp"

#include <algorithm>
#include <cctype>

namespace launcher::config {

InputBindings defaultInputBindings() {
  InputBindings bindings;
  bindings.keyboard = {SDLK_z,    SDLK_x,    SDLK_RSHIFT, SDLK_RETURN,
                       SDLK_UP,   SDLK_DOWN, SDLK_LEFT,   SDLK_RIGHT,
                       SDLK_c,    SDLK_v,    SDLK_f,      SDLK_j,
                       SDLK_q,    SDLK_e};
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
  // Player 2 shares the pad layout (a second pad uses the same buttons) but
  // gets its own keys so one keyboard can drive both ports.
  bindings.keyboardPlayer2 = {SDLK_g, SDLK_h, SDLK_r, SDLK_t,
                              SDLK_w, SDLK_s, SDLK_a, SDLK_d,
                              SDLK_b, SDLK_n, SDLK_i, SDLK_k,
                              SDLK_o, SDLK_p};
  bindings.controllerPlayer2 = bindings.controller;
  bindings.analogDeadZone = 16000;
  return bindings;
}

std::array<SDL_Keycode, kInputActionCount> &
keyboardBindings(InputBindings &bindings, std::size_t player) noexcept {
  return player == 0 ? bindings.keyboard : bindings.keyboardPlayer2;
}

const std::array<SDL_Keycode, kInputActionCount> &
keyboardBindings(const InputBindings &bindings, std::size_t player) noexcept {
  return player == 0 ? bindings.keyboard : bindings.keyboardPlayer2;
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

InputPrompts inputPrompts(InputDeviceKind device, ControllerStyle style) {
  if (device == InputDeviceKind::KeyboardMouse)
    return {"ENTER", "X", "ESC", "TAB", "ENTER", "LEFT/RIGHT", "."};
  std::string category = "LB/RB";
  if (style == ControllerStyle::Nintendo)
    category = "L/R";
  else if (style == ControllerStyle::PlayStation)
    category = "L1/R1";
  return {controllerButtonPrompt(SDL_CONTROLLER_BUTTON_A, style),
          controllerButtonPrompt(SDL_CONTROLLER_BUTTON_X, style),
          controllerButtonPrompt(SDL_CONTROLLER_BUTTON_B, style),
          category,
          controllerButtonPrompt(SDL_CONTROLLER_BUTTON_A, style),
          "D-PAD",
          "RB"};
}

} // namespace launcher::config
