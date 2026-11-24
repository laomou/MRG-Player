#pragma once

#include <SDL.h>

#include <array>
#include <cstddef>

namespace launcher {

struct Game {
  const char *title;
  const char *genre;
  const char *year;
  const char *progress;
  int minutes;
  SDL_Color top;
  SDL_Color bottom;
  SDL_Color accent;
};

inline constexpr std::size_t kGameCount = 7;

const std::array<Game, kGameCount> &games();

} // namespace launcher
