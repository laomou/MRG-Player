#pragma once

#include <SDL.h>

#include <string_view>

#include "config/InputBindings.hpp"
#include "i18n/Localization.hpp"
#include "library/GameLibrary.hpp"

namespace launcher::ui {

void drawHome(SDL_Renderer *renderer, int selectedGame, float time,
              Uint64 toastUntil, std::string_view toastMessage, int startupPage,
              i18n::Language language, const library::GameLibrary &gameLibrary,
              const config::InputPrompts &prompts);
void moveSelection(int &selectedGame, int direction, int count);

} // namespace launcher::ui
