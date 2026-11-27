#pragma once

#include <SDL.h>

#include "i18n/Localization.hpp"
#include "library/GameLibrary.hpp"

namespace launcher::ui {

enum class ManagementDialog { Menu, ConfirmRemoveRom, ConfirmDeleteSave, Info };

inline constexpr int kManagementActionCount = 6;

void drawGameManagementOverlay(SDL_Renderer *renderer,
                               const library::GameEntry &entry,
                               int selectedAction, ManagementDialog dialog,
                               int confirmationChoice, bool saveDataPresent,
                               i18n::Language language, SDL_Color accent);
int managementActionAt(float x, float y);
int confirmationChoiceAt(float x, float y);
bool managementInfoBackAt(float x, float y);

} // namespace launcher::ui
