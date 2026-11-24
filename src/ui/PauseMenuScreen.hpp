#pragma once

#include <SDL.h>

#include <array>
#include <string>
#include <string_view>

#include "config/InputBindings.hpp"
#include "i18n/Localization.hpp"

namespace launcher::ui {

inline constexpr int kPauseActionCount = 7;
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
                   const config::InputPrompts &prompts);
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
                         const config::InputPrompts &prompts);
int pauseVideoRowAt(float x, float y);

// Power cycling wipes unsaved progress, so it asks first. Choice 0 cancels
// (the default) and choice 1 confirms.
void drawPowerCycleConfirm(SDL_Renderer *renderer, int choice,
                           i18n::Language language,
                           const config::InputPrompts &prompts);
int powerCycleChoiceAt(float x, float y);

} // namespace launcher::ui
