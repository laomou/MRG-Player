#pragma once

#include <SDL.h>

#include <string_view>

#include "config/InputBindings.hpp"
#include "ui/CoverSource.hpp"
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
              int focusedPill, int focusedAction, float rowOffset,
              float focusOpacity, SDL_Color accent,
              CoverSource *covers = nullptr);
void moveSelection(int &selectedGame, int direction, int count);

// Two actions when a game is selected, none when the library is empty.
[[nodiscard]] int homeActionCount(bool hasGame);
[[nodiscard]] int homeActionAt(float x, float y, bool hasGame,
                               i18n::Language language,
                               const config::InputPrompts &prompts);
[[nodiscard]] int homeCardAt(float x, float y, int count, float rowOffset);
// How far the shelf has to slide for the selection to stay on screen. It is a
// fraction while the row is travelling, so callers can animate towards it.
[[nodiscard]] float homeRowTarget(int selectedGame, int count);
// 0 is the library pill, 1 is the settings pill; -1 when neither was hit. The
// pills are sized from their labels, so a hit test needs the same language.
[[nodiscard]] int homePillAt(float x, float y, i18n::Language language,
                             std::size_t count);

// The launcher asks before it closes: 0 is the cancel button, 1 the quit one.
void drawQuitConfirm(SDL_Renderer *renderer, int choice,
                     i18n::Language language,
                     const config::InputPrompts &prompts, SDL_Color accent);
[[nodiscard]] int quitConfirmChoiceAt(float x, float y);

} // namespace launcher::ui
