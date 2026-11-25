#pragma once

#include <SDL.h>

#include "library/GameLibrary.hpp"

namespace launcher::ui {

// What the home screen needs from box art. The real cache downloads and
// decodes; tests hand in nothing, and both see the same null when a game has
// no cover yet.
class CoverSource {
 public:
  virtual ~CoverSource() = default;
  [[nodiscard]] virtual SDL_Texture *cover(SDL_Renderer *renderer, const library::GameEntry &game) = 0;
  [[nodiscard]] virtual SDL_Texture *wallpaper(SDL_Renderer *renderer, const library::GameEntry &game) = 0;
};

}  // namespace launcher::ui
