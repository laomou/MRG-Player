#pragma once

#include <SDL.h>

#include <string_view>

#include "config/InputBindings.hpp"
#include "ui/CoverSource.hpp"
#include "i18n/Localization.hpp"
#include "library/GameLibrary.hpp"

namespace launcher::ui {

// Which part of the home screen the direction keys drive: the pills or the
// shelf. The strip along the bottom only names the keys, so the keys never
// stop on it.
enum class HomeFocus { Cards, Pills };

void drawHome(SDL_Renderer *renderer, int selectedGame, Uint64 toastUntil, std::string_view toastMessage, int startupPage,
              i18n::Language language, const library::GameLibrary &gameLibrary,
              const config::InputPrompts &prompts, HomeFocus focus,
              int focusedPill, float rowOffset,
              float focusOpacity, SDL_Color accent,
              CoverSource *covers = nullptr, bool showFooter = true,
              bool showPills = true);
void moveSelection(int &selectedGame, int direction, int count);

// The strip names the two things the pointer can do with the selected card --
// start it on the right, open its drawer on the left. The entries are buttons
// for the mouse, but they never take focus: the keys stay on the cards, so the
// strip is always drawn idle.
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
// The dialog is only its two buttons -- cancel left, confirm right -- so it
// needs no key prompts.
void drawQuitConfirm(SDL_Renderer *renderer, int choice,
                     i18n::Language language, SDL_Color accent);
[[nodiscard]] int quitConfirmChoiceAt(float x, float y);

} // namespace launcher::ui
