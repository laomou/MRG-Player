#include "input/BrickInput.hpp"

#include <cstdlib>

namespace launcher::input {
namespace {

constexpr Sint16 kPressThreshold = 22000;
constexpr Sint16 kReleaseThreshold = 12000;

std::optional<SDL_GameControllerButton> uiButton(Uint8 button) noexcept {
  switch (button) {
  case kBrickButtonA:
    return SDL_CONTROLLER_BUTTON_A;
  case kBrickButtonB:
    return SDL_CONTROLLER_BUTTON_B;
  case kBrickButtonY:
    return SDL_CONTROLLER_BUTTON_Y;
  case kBrickButtonX:
    return SDL_CONTROLLER_BUTTON_X;
  case kBrickButtonL1:
    return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
  case kBrickButtonR1:
    return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
  case kBrickButtonSelect:
    return SDL_CONTROLLER_BUTTON_BACK;
  case kBrickButtonStart:
    return SDL_CONTROLLER_BUTTON_START;
  case kBrickButtonMenu:
    return SDL_CONTROLLER_BUTTON_B;
  case kBrickButtonL3:
    return SDL_CONTROLLER_BUTTON_LEFTSTICK;
  case kBrickButtonR3:
    return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
  default:
    return std::nullopt;
  }
}

BrickUiTranslation controllerButtonEvent(const SDL_Event &source,
                                         SDL_GameControllerButton button) {
  BrickUiTranslation result;
  result.consumed = true;
  result.hasEvent = true;
  result.event.type = source.type == SDL_JOYBUTTONDOWN
                          ? SDL_CONTROLLERBUTTONDOWN
                          : SDL_CONTROLLERBUTTONUP;
  result.event.cbutton.timestamp = source.jbutton.timestamp;
  result.event.cbutton.which = source.jbutton.which;
  result.event.cbutton.button = static_cast<Uint8>(button);
  result.event.cbutton.state = source.jbutton.state;
  return result;
}

int axisDirection(Sint16 value, int previous) noexcept {
  if (value < -kPressThreshold)
    return -1;
  if (value > kPressThreshold)
    return 1;
  if (std::abs(static_cast<int>(value)) < kReleaseThreshold)
    return 0;
  return previous;
}

} // namespace

BrickUiTranslation BrickInputAdapter::translateForUi(const SDL_Event &source) {
  BrickUiTranslation result;
  if (source.type == SDL_JOYBUTTONDOWN || source.type == SDL_JOYBUTTONUP) {
    result.consumed = true;
    if (source.jbutton.button == kBrickButtonVolumeDown) {
      if (source.type == SDL_JOYBUTTONDOWN)
        result.command = BrickUiCommand::VolumeDown;
      return result;
    }
    if (source.jbutton.button == kBrickButtonVolumeUp) {
      if (source.type == SDL_JOYBUTTONDOWN)
        result.command = BrickUiCommand::VolumeUp;
      return result;
    }
    const auto button = uiButton(source.jbutton.button);
    return button ? controllerButtonEvent(source, *button) : result;
  }
  if (source.type != SDL_JOYAXISMOTION)
    return result;

  result.consumed = true;
  int *previous = nullptr;
  SDL_GameControllerButton negative = SDL_CONTROLLER_BUTTON_INVALID;
  SDL_GameControllerButton positive = SDL_CONTROLLER_BUTTON_INVALID;
  if (source.jaxis.axis == 0) {
    previous = &horizontal_;
    negative = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    positive = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
  } else if (source.jaxis.axis == 1) {
    previous = &vertical_;
    negative = SDL_CONTROLLER_BUTTON_DPAD_UP;
    positive = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
  } else {
    return result;
  }

  const int direction = axisDirection(source.jaxis.value, *previous);
  if (direction == *previous)
    return result;
  *previous = direction;
  if (direction == 0)
    return result;

  result.hasEvent = true;
  result.event.type = SDL_CONTROLLERBUTTONDOWN;
  result.event.cbutton.timestamp = source.jaxis.timestamp;
  result.event.cbutton.which = source.jaxis.which;
  result.event.cbutton.button = static_cast<Uint8>(direction < 0 ? negative : positive);
  result.event.cbutton.state = SDL_PRESSED;
  return result;
}

void BrickInputAdapter::reset() noexcept {
  horizontal_ = 0;
  vertical_ = 0;
}

std::optional<config::InputAction> brickButtonAction(Uint8 button) noexcept {
  switch (button) {
  case kBrickButtonA:
    return config::InputAction::A;
  case kBrickButtonB:
    return config::InputAction::B;
  case kBrickButtonY:
    return config::InputAction::Y;
  case kBrickButtonX:
    return config::InputAction::X;
  case kBrickButtonL1:
    return config::InputAction::L;
  case kBrickButtonR1:
    return config::InputAction::R;
  case kBrickButtonSelect:
    return config::InputAction::Select;
  case kBrickButtonStart:
    return config::InputAction::Start;
  case kBrickButtonL3:
    return config::InputAction::TurboA;
  case kBrickButtonR3:
    return config::InputAction::TurboB;
  default:
    return std::nullopt;
  }
}

std::optional<SDL_GameControllerAxis> brickAxis(Uint8 axis) noexcept {
  switch (axis) {
  case 0:
    return SDL_CONTROLLER_AXIS_LEFTX;
  case 1:
    return SDL_CONTROLLER_AXIS_LEFTY;
  case 2:
    return SDL_CONTROLLER_AXIS_TRIGGERLEFT;
  case 3:
    return SDL_CONTROLLER_AXIS_RIGHTX;
  case 4:
    return SDL_CONTROLLER_AXIS_RIGHTY;
  case 5:
    return SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
  default:
    return std::nullopt;
  }
}

bool brickMenuPressed(const SDL_Event &event) noexcept {
  return event.type == SDL_JOYBUTTONDOWN &&
         event.jbutton.button == kBrickButtonMenu;
}

} // namespace launcher::input
