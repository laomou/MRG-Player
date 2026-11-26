#pragma once

#include "config/InputBindings.hpp"
#include "game/GamePlatform.hpp"
#include "i18n/Localization.hpp"
#include "ui/Layout.hpp"

#include <SDL.h>
#include <string>
#include <vector>

namespace launcher::ui {

enum class ControlHelpDevice { Keyboard, Controller, Brick };
struct ControlHelpRow {
  std::string binding;
  std::string button;
};
struct ControlHelp {
  std::string subtitle;
  std::string source;
  std::string pauseKey;
  std::vector<ControlHelpRow> rows;
};

// P1 only, explicitly labelled. Brick raw buttons bypass configurable SDL
// controller bindings, exactly as they do in gameplay.
ControlHelp gameControlHelp(game::GamePlatform platform,
                            const config::InputBindings &bindings,
                            ControlHelpDevice device,
                            config::ControllerStyle style,
                            i18n::Language language);

inline constexpr int kControlHelpRows = 7;
struct ControlHelpLayout {
  Rect panel;
  LinearLayout rows;
  Rect back;
};
ControlHelpLayout controlHelpLayout();
int controlHelpPageCount(const ControlHelp &help);
bool controlHelpBackAt(float x, float y);
// Confirm activates the sole focused Back button. Directions page long custom
// mappings; no new shortcut is needed to enter or leave this screen.
bool controlHelpBackEvent(const SDL_Event &event);
int controlHelpPageStep(const SDL_Event &event);
void drawControlHelp(SDL_Renderer *renderer, const ControlHelp &help, int page,
                     i18n::Language language,
                     const config::InputPrompts &prompts, SDL_Color accent);

// Elapsed gameplay time, not a deadline: starting at tick zero works too.
float gameEntryHintOpacity(Uint64 elapsedMs);
void drawGameEntryHint(SDL_Renderer *renderer, const std::string &pauseKey,
                       Uint64 elapsedMs, i18n::Language language,
                       SDL_Color accent);

} // namespace launcher::ui
