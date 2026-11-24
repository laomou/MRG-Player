#pragma once

#include "config/Settings.hpp"

#include <SDL.h>

#include <string>
#include <string_view>
#include <vector>

namespace launcher::ui {

// One line of the core installer page: which platform it serves, the preferred
// core, and the state that line is in right now.
struct CoreRow {
  std::string platform;
  std::string core;
  std::string status;
  bool installed = false;
};

int settingCategoryCount();
int settingRowCount(int category);
// Maps a point in logical coordinates to the controller page row under it, or
// -1 when the point misses every row. The binding list is two columns wide, so
// hit testing has to know the exact geometry the page draws with.
int controllerPageRowAt(int x, int y);
void changeSetting(AppSettings &settings, int category, int row, int direction);
void drawSettings(SDL_Renderer *renderer, const AppSettings &settings,
                  int category, int row, int selectedGame, float time,
                  bool bindingCapture, std::string_view activeControllerName,
                  config::ControllerStyle resolvedControllerStyle,
                  const std::vector<CoreRow> &cores,
                  const config::InputPrompts &prompts);

} // namespace launcher::ui
