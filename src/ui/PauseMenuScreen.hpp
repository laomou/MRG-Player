#pragma once

#include <SDL.h>

#include <array>
#include <string>
#include <string_view>

#include "config/InputBindings.hpp"
#include "i18n/Localization.hpp"
#include "runtime/CoreOptions.hpp"

namespace launcher::ui {

inline constexpr int kPauseActionCount = 9;
inline constexpr int kPauseControlHelpAction = 7;
inline constexpr int kPauseReturnHomeAction = 8;
inline constexpr int kStateSlotCount = 3;

// What the pause menu shows for one save-state slot. The thumbnail texture is
// owned by the caller and may be null when a snapshot has no image.
struct StateSlotView {
  bool present = false;
  std::string savedAt;
  std::string playTime;
  SDL_Texture *thumbnail = nullptr;
};

void drawPauseMenu(SDL_Renderer *renderer, std::string_view gameTitle,
                   int selectedAction, int stateSlot,
                   const std::array<StateSlotView, kStateSlotCount> &slots,
                   std::string_view feedback, Uint64 feedbackUntil,
                   i18n::Language language,
                   const config::InputPrompts &prompts, SDL_Color accent);
int pauseActionAt(float x, float y);
int pauseSlotAt(float x, float y);

// The display panel replaces the slot picker and the action list while it is
// open, so a game can be re-fitted to the screen without leaving it.
inline constexpr int kPauseVideoRowCount = 4;

struct PauseVideoState {
  int aspect = 0;
  bool integerScale = false;
  int overscan = 0;
  bool overrideEnabled = false;
};

void drawPauseVideoPanel(SDL_Renderer *renderer, int row,
                         const PauseVideoState &video, i18n::Language language,
                         const config::InputPrompts &prompts, SDL_Color accent);
int pauseVideoRowAt(float x, float y);

// The option panel lists the settings the running core declares. Six fit on
// the card at once and the rest scroll, so a core with dozens of options stays
// navigable with the same controls as the display panel.
inline constexpr int kPauseCoreOptionRows = 6;

void drawPauseCoreOptionsPanel(SDL_Renderer *renderer, int cursor, int scroll,
                               const runtime::CoreOptionList &options,
                               i18n::Language language,
                               const config::InputPrompts &prompts,
                               SDL_Color accent);
int pauseCoreOptionAt(float x, float y, int scroll);

// Power cycling wipes unsaved progress, so it asks first. Choice 0 cancels
// (the default) and choice 1 confirms.
void drawPowerCycleConfirm(SDL_Renderer *renderer, int choice,
                           i18n::Language language, SDL_Color accent);
int powerCycleChoiceAt(float x, float y);

} // namespace launcher::ui
