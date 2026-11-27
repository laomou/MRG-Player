#pragma once

#include "config/Settings.hpp"
#include "i18n/Localization.hpp"
#include "ui/Controls.hpp"
#include "ui/Layout.hpp"
#include "ui/Primitives.hpp"

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
  // Human-readable size of the installed library; empty when nothing is
  // installed.
  std::string size;
  bool installed = false;
};

// Horizontal bounds for the variable-width fields on a core row. Keeping them
// together guarantees that a long filename is fitted before the status pill.
struct CoreRowColumns {
  Rect filename;
  Rect status;
};
[[nodiscard]] CoreRowColumns coreRowColumns(const std::string &status);

int settingCategoryCount();
// Counts the rows the page actually offers. The general page drops its rewind
// line when the prompts come from a pad: that switch flips a shortcut only a
// held keyboard key can start, so it is not offered where no such key exists.
int settingRowCount(int category, bool keyboardPrompts);
// The refresh button is a control rather than a list row, so focus order needs
// an id that cannot collide with a row index.
inline constexpr int kSettingsRefreshControlId = 1000;
// Focus order of the page: column 0 is the category rail, column 1 is the rows
// of the current page. The core page puts its refresh button at the front of
// that column, so pressing up from the first platform reaches the button
// without a special case in the input code.
[[nodiscard]] FocusGrid settingsControls(int category, bool keyboardPrompts);
// Maps a point in logical coordinates to the controller page row under it, or
// -1 when the point misses every row. The binding list is two columns wide, so
// hit testing has to know the exact geometry the page draws with.
int controllerPageRowAt(int x, int y);
// Same idea for the general page, whose rows are shorter than the others so
// the rewind and runahead options fit above the footer.
int generalPageRowAt(float x, float y, bool keyboardPrompts);
// The category rail and the settings rows are the clickable controls on this
// screen, so both answer a pointer with the index it lands on, or -1.
int settingsCategoryAt(float x, float y);
int settingsRowAt(int category, float x, float y, bool keyboardPrompts);
// The core page also carries a refresh button on its heading line, which is a
// button rather than a row, so it answers a hit test of its own.
bool coreRefreshAt(float x, float y);

// The legend under the page: back on the left, and whatever this page does
// with the focused row on the right. The labels differ per page, so the row is
// built here instead of at every call site. It is a legend, not a control: the
// pointer and the direction keys only ever touch the rail and the rows.
[[nodiscard]] std::vector<Hint> settingsFooterHints(
    int category, int row, const std::vector<CoreRow> &cores,
    bool refreshFocused, i18n::Language language,
    const config::InputPrompts &prompts);
// Which entry of that strip a point lands on, or -1. 0 is back, 1 the action.
// The strip answers the pointer without ever holding the keys.
[[nodiscard]] int settingsFooterAt(float x, float y,
                                   const std::vector<Hint> &hints);
void changeSetting(AppSettings &settings, int category, int row, int direction,
                   bool keyboardPrompts);
void drawSettings(SDL_Renderer *renderer, const AppSettings &settings,
                  int category, int row, bool bindingCapture, std::string_view activeControllerName,
                  config::ControllerStyle resolvedControllerStyle,
                  const std::vector<CoreRow> &cores,
                  const config::InputPrompts &prompts, bool railFocused,
                  float focusOpacity, bool refreshFocused = false);

} // namespace launcher::ui
