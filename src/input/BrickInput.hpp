#pragma once

#include <SDL.h>

#include <optional>

#include "config/InputBindings.hpp"

namespace launcher::input {

inline constexpr Uint8 kBrickButtonB = 0;
inline constexpr Uint8 kBrickButtonA = 1;
inline constexpr Uint8 kBrickButtonY = 2;
inline constexpr Uint8 kBrickButtonX = 3;
inline constexpr Uint8 kBrickButtonL1 = 4;
inline constexpr Uint8 kBrickButtonR1 = 5;
inline constexpr Uint8 kBrickButtonSelect = 6;
inline constexpr Uint8 kBrickButtonStart = 7;
inline constexpr Uint8 kBrickButtonMenu = 8;
inline constexpr Uint8 kBrickButtonL3 = 9;
inline constexpr Uint8 kBrickButtonR3 = 10;
inline constexpr Uint8 kBrickButtonVolumeDown = 13;
inline constexpr Uint8 kBrickButtonVolumeUp = 14;

enum class BrickUiCommand { None, VolumeDown, VolumeUp };

struct BrickUiTranslation {
  bool consumed = false;
  bool hasEvent = false;
  SDL_Event event{};
  BrickUiCommand command = BrickUiCommand::None;
};

// Converts the Brick's raw joystick labels to the controller semantics used by
// launcher screens. Gameplay deliberately does not use this translation:
// physical A/B/X/Y must remain console A/B/X/Y there.
class BrickInputAdapter {
public:
  [[nodiscard]] BrickUiTranslation translateForUi(const SDL_Event &source);
  void reset() noexcept;

private:
  int horizontal_ = 0;
  int vertical_ = 0;
};

[[nodiscard]] std::optional<config::InputAction>
brickButtonAction(Uint8 button) noexcept;
[[nodiscard]] std::optional<SDL_GameControllerAxis>
brickAxis(Uint8 axis) noexcept;
[[nodiscard]] bool brickMenuPressed(const SDL_Event &event) noexcept;

} // namespace launcher::input
