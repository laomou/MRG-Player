#pragma once

#include <SDL.h>

#include <string_view>

#include "config/InputBindings.hpp"
#include "i18n/Localization.hpp"
#include "library/GameLibrary.hpp"

namespace launcher::ui {

// Which part of the home screen the direction keys drive. The card row is the
// default; down moves onto the footer, the way a console lets you walk off the
// shelf and onto its buttons.
enum class HomeFocus { Cards, Pills, Actions };

void drawHome(SDL_Renderer *renderer, int selectedGame, float time,
              Uint64 toastUntil, std::string_view toastMessage, int startupPage,
              i18n::Language language, const library::GameLibrary &gameLibrary,
              const config::InputPrompts &prompts, HomeFocus focus,
              int focusedPill, int focusedAction);
void moveSelection(int &selectedGame, int direction, int count);

// The footer grows and shrinks with the selection: an empty slot has nothing to
// start or manage, so only the quit entry remains.
[[nodiscard]] int homeActionCount(bool hasGame);
[[nodiscard]] int homeActionAt(float x, float y, bool hasGame,
                               i18n::Language language,
                               const config::InputPrompts &prompts);
[[nodiscard]] int homeCardAt(float x, float y, int count);
// 0 is the library pill, 1 is the settings pill; -1 when neither was hit.
[[nodiscard]] int homePillAt(float x, float y);

} // namespace launcher::ui
