#include "ui/GameManagementScreen.hpp"
#include "ui/HomeScreen.hpp"
#include "ui/PauseMenuScreen.hpp"
#include "ui/Primitives.hpp"
#include "ui/SettingsScreen.hpp"
#include "ui/Theme.hpp"
#include <SDL.h>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool result, const char *message) {
  if (!result) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}
} // namespace

int main(int argc, char **argv) {
  using namespace launcher::ui;
  check(argc == 2, "Font path required");
  check(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
  check(initializeTextRenderer(argv[1]), "Font initialization failed");
  check(textWidth("", 1) == 0, "Empty text width");
  check(textWidth("Read me", 1) > 42,
        "Body text should no longer use tiny bitmap font");
  check(textWidth("iii", 2) < textWidth("WWW", 2),
        "Latin font should be proportional");
  check(textWidth("中文", 2) > 30, "CJK font should remain available");
  check(textWidth("A中文B", 2) ==
            textWidth("A", 2) + textWidth("中文", 2) + textWidth("B", 2),
        "Mixed-script measurement must match rendering runs");
  for (int slot = 0; slot < kStateSlotCount; ++slot) {
    check(pauseSlotAt(1012 + slot * 164 + 76, 300) == slot, "Slot hit target");
    check(pauseSlotAt(1012 + slot * 164 + 158, 300) == -1,
          "Slot gap must not activate");
  }
  check(pauseSlotAt(1100, 270) == -1, "Outside slots");
  for (int action = 0; action < kPauseActionCount; ++action)
    check(pauseActionAt(1200, 396 + action * 48 + 22) == action,
          "Pause action hit target");
  check(kPauseActionCount == 8,
        "The pause card lists resume, states, reset, display, options, home");
  for (int row = 0; row < kPauseCoreOptionRows; ++row)
    check(pauseCoreOptionAt(1200, 300 + row * 56 + 25, 0) == row,
          "Core option row hit target");
  check(pauseCoreOptionAt(1200, 300 + 25, 4) == 4,
        "Core option hit target follows the scroll offset");
  check(pauseCoreOptionAt(500, 300, 0) == -1,
        "Gameplay area must not activate core option rows");
  for (int row = 0; row < kPauseVideoRowCount; ++row)
    check(pauseVideoRowAt(1200, 300 + row * 80 + 35) == row,
          "Display row hit target");
  check(pauseVideoRowAt(500, 300) == -1,
        "Gameplay area must not activate display rows");
  check(pauseActionAt(500, 450) == -1,
        "Gameplay area must not activate actions");
  for (int row = 0; row < settingRowCount(0); ++row)
    check(generalPageRowAt(1200, 376 + row * 84 + 20) == row,
          "General row hit target");
  check(generalPageRowAt(1200, 376 + settingRowCount(0) * 84) == -1,
        "General hit testing must stop after the last row");
  check(generalPageRowAt(300, 420) == -1,
        "The settings sidebar must not activate general rows");
  // Every clickable control on the settings screen answers a click: the
  // category rail and the rows of each page.
  check(settingsCategoryAt(200, 351 + 2 * 52 + 10) == 2,
        "The category rail maps a click to its entry");
  check(settingsCategoryAt(200, 790) == -1,
        "Below the rail activates nothing");
  check(settingsCategoryAt(200, 351 + 7 * 52 + 10) == 7,
        "The last entry on the rail is about");
  check(settingsCategoryAt(500, 420) == -1,
        "The settings body must not activate categories");
  check(settingsRowAt(1, 1200, 382 + 3 * 106 + 10) == 3,
        "Every display row is clickable, not just the first");
  check(settingsRowAt(1, 1200, 382 + 3 * 106 + 100) == -1,
        "The gap under the last display row activates nothing");
  check(settingsRowAt(2, 1200, 382 + 106 + 10) == 1,
        "Audio rows map to their own index");
  // The controller page keeps two binding columns on a 34 pixel pitch: the line
  // decides the action, the half of the track decides which of the two pads it
  // belongs to, and the dead zone closes the list under both columns.
  check(settingsRowAt(3, 700, 372 + 4 * 34 + 10) == 4,
        "The left binding column maps to its own action");
  check(settingsRowAt(3, 1200, 372 + 4 * 34 + 10) == 11,
        "The right binding column maps to its own action");
  check(settingsRowAt(3, 700, 372 + 10) == 0,
        "The controller page starts with its own device rows");
  check(settingsRowAt(3, 700, 372 + 11 * 34 + 10) == 18,
        "The dead zone closes the controller list");
  check(settingsRowAt(3, 700, 372 + 11 * 34 + 31) == -1,
        "The gap under the last controller line activates nothing");
  check(settingsRowAt(4, 300, 420) == -1,
        "Outside the row track activates nothing");
  check(settingsRowAt(6, 1200, 420) == 0, "The theme row is clickable");
  check(settingsRowAt(7, 1200, 420) == 0, "The about row is clickable");
  const SDL_Color teal = accentColor(0);
  check(teal.r == 86 && teal.g == 223 && teal.b == 202,
        "The default accent is the logo teal");
  check(accentColor(kAccentCount).b == teal.b,
        "The accent index wraps instead of falling off the table");
  check(settingCategoryCount() == 8,
        "Theme sits between the cores page and about");
  check(powerCycleChoiceAt(600, 530) == 0,
        "Power-cycle dialog defaults to cancel");
  check(powerCycleChoiceAt(900, 530) == 1, "Power-cycle confirmation target");
  check(powerCycleChoiceAt(600, 300) == -1, "Dialog text must not confirm");
  for (int action = 0; action < kManagementActionCount; ++action)
    check(managementActionAt(1200, 248 + action * 86 + (action >= 4 ? 18 : 0) +
                                       34) == action,
          "Management hit target must follow display order");
  check(managementActionAt(1200, 586) == -1,
        "Danger section separator must not activate");
  check(confirmationChoiceAt(500, 620) == 0, "Cancel target preserved");
  check(confirmationChoiceAt(950, 620) == 1, "Confirmation target preserved");

  // The home screen lets the direction keys walk the pills, the shelf, and the
  // footer, so every on-screen button has a hit target of its own.
  check(homePillAt(1200, 80, launcher::i18n::Language::English, 4) == 0,
        "Library pill target preserved");
  check(homePillAt(1450, 80, launcher::i18n::Language::English, 4) == 1,
        "Settings pill target preserved");
  check(homePillAt(700, 80, launcher::i18n::Language::English, 4) == -1,
        "Empty header space activates nothing");
  // The pills are sized from their labels, so the longest translation still
  // ends inside the right margin instead of running off the canvas.
  check(homePillAt(1500, 80, launcher::i18n::Language::English, 4) == 1,
        "The settings pill reaches the right margin");
  check(homePillAt(1120, 80, launcher::i18n::Language::SimplifiedChinese,
                   4) == 0,
        "The Chinese pills keep their old geometry");
  check(homeCardAt(500, 500, 7, 0.0f) >= 0,
        "A card in the row is clickable");
  check(homeCardAt(500, 200, 7, 0.0f) == -1,
        "Above the row activates nothing");
  // Seven and a half cards fit: the last one is always half visible so the
  // shelf shows there is more to the right, and the row never scrolls past its
  // last card.
  check(homeRowTarget(0, 5) == 0.0f && homeRowTarget(6, 7) == 0.0f,
        "A short library never scrolls");
  check(homeRowTarget(8, 10) == 2.0f && homeRowTarget(9, 10) == 2.5f,
        "A long library stops with the last card flush against the right edge");
  check(homeCardAt(1500, 500, 10, 2.0f) >= 7,
        "Scrolling the row moves the cards under the pointer");
  launcher::config::InputPrompts prompts;
  check(homeActionAt(1500, 828, true, launcher::i18n::Language::English,
                     prompts) == 1,
        "The last footer entry is the rightmost button");
  check(homeActionAt(700, 828, true, launcher::i18n::Language::English,
                     prompts) == -1,
        "Empty footer space activates nothing");
  check(homeActionCount(true) == 2 && homeActionCount(false) == 0,
        "Only a real game offers start and manage");
  // The settings screen keeps the categories and the page apart: the rail is
  // where the focus starts, the page is reached with right.
  check(settingsCategoryAt(200, 351 + 2 * 52 + 10) == 2,
        "The category rail hit target follows the list");
  check(settingsRowAt(1, 1200, 382 + 2 * 106 + 20) == 2,
        "The display page rows are all clickable");
  // The core page gives every platform its own line, and the refresh button
  // sits on the heading line instead of inside one, so both answer a hit test.
  check(settingsRowAt(5, 1200, 396 + 2 * 56 + 20) == 2,
        "The core list maps a click to its platform");
  check(settingsRowAt(5, 1200, 396 + 48 + 4) == -1,
        "The gap between core rows activates nothing");
  check(settingsRowAt(5, 1200, 300) == -1,
        "The core heading line is not a row");
  check(coreRefreshAt(1400, 310) && !coreRefreshAt(1400, 396),
        "The refresh button is its own target");

  // The widgets speak canvas units, but they are drawn in output pixels, so a
  // window that is wider than 16:9 letterboxes the canvas and the pointer maps
  // back through the same transform.
  setOutputSize(2000, 900);
  float canvasX = 0.0f;
  float canvasY = 0.0f;
  windowToCanvas(1000.0f, 450.0f, canvasX, canvasY);
  check(std::abs(canvasX - 800.0f) < 0.01f &&
            std::abs(canvasY - 450.0f) < 0.01f,
        "Window coordinates map back onto the canvas");
  float canvasLeft = 0.0f;
  float canvasTop = 0.0f;
  float canvasWidth = 0.0f;
  float canvasHeight = 0.0f;
  canvasRect(canvasLeft, canvasTop, canvasWidth, canvasHeight);
  check(std::abs(canvasLeft - 200.0f) < 0.5f &&
            std::abs(canvasWidth - 1600.0f) < 0.5f &&
            std::abs(canvasHeight - 900.0f) < 0.5f,
        "A 2000x900 window letterboxes the canvas on both sides");
  // A scaled display reports window points that are smaller than the pixels the
  // canvas is drawn in, so the pointer is scaled up before the transform runs.
  setOutputSize(3200, 1800);
  setWindowSize(1600, 900);
  windowToCanvas(800.0f, 450.0f, canvasX, canvasY);
  check(std::abs(canvasX - 800.0f) < 0.01f &&
            std::abs(canvasY - 450.0f) < 0.01f,
        "A 2x display still maps the window centre onto the canvas centre");
  setWindowSize(0, 0);
  setOutputSize(1600, 900);

  // Focus order is data, not code paths: the rail and the page are two columns
  // and the core page's refresh button is the first entry of the page column,
  // so up from the first platform reaches it without a special case.
  {
    FocusGrid grid = settingsControls(5);
    check(grid.listCount() == 2, "The settings page has a rail and a page");
    check(grid.listIndex() == 0, "The rail owns the keys first");
    check(grid.focusedId() == 0, "The rail starts on the first category");
    check(grid.list(1).focusedId() == kSettingsRefreshControlId,
          "The core page puts the refresh button first in the page column");
    check(grid.focusList(1), "Right moves the keys into the page");
    grid.list(1).focusIndex(1);
    check(grid.focusedId() == 0, "Index one is the first platform, not the button");
    check(grid.moveVertical(1) && grid.focusedId() == 1,
          "Down walks the platform rows");
    check(grid.moveVertical(-1) && grid.focusedId() == 0,
          "Up walks back to the first platform");
    check(grid.moveVertical(-1) &&
              grid.focusedId() == kSettingsRefreshControlId,
          "Up from the first platform reaches the refresh button");
    check(grid.moveVertical(1) && grid.focusedId() == 0,
          "Down from the button returns to the first platform");
    check(grid.focusList(0) && grid.moveVertical(-1) && grid.focusedId() == 7,
          "The category rail wraps upwards");
    check(grid.moveVertical(1) && grid.focusedId() == 0,
          "The category rail wraps downwards");
    check(settingsControls(0).list(1).indexOfId(kSettingsRefreshControlId) ==
              -1,
          "Only the core page carries the refresh button");
    check(settingsControls(0).list(1).focusedId() == 0,
          "Other pages start on their first row");
    FocusGrid core = settingsControls(5);
    (void)core.focusList(1);
    core.list(1).focusIndex(core.list(1).count() - 1);
    check(core.focusedId() == 4, "The last core row is the last entry");
    check(!core.moveVertical(1),
          "The core page does not wrap from the last row to the button");
    core.list(1).focusIndex(0);
    check(!core.moveVertical(-1),
          "The refresh button is the top of the core page column");
  }
  {
    ControlList list;
    list.reset({{0, true}, {1, false}, {2, true}});
    check(list.focusedId() == 0, "The first enabled control takes focus");
    check(list.move(1) && list.focusedId() == 2,
          "A disabled control is skipped, not focused");
    check(list.move(1) && list.focusedId() == 0, "A list wraps at the end");
    check(!list.move(0), "A zero step is not a move");
    list.focusIndex(1);
    check(list.focusedId() == 1,
          "A pointer may park on a control the keys cannot reach");
    list.focusIndex(3);
    check(list.focusedId() == 1, "Out of range focus is ignored");
    ControlList empty;
    empty.reset({});
    check(empty.empty() && empty.focusedId() == -1 && !empty.move(1),
          "An empty list takes no focus and no movement");
    ControlList clamped;
    clamped.reset({{0, true}, {1, true}}, false);
    check(!clamped.move(-1), "A clamped list stops at its first control");
    check(clamped.move(1) && clamped.focusedId() == 1,
          "A clamped list still moves forwards");
    check(!clamped.move(1), "A clamped list stops at its last control");
  }

  SDL_Surface *surface =
      SDL_CreateRGBSurfaceWithFormat(0, 100, 100, 32, SDL_PIXELFORMAT_RGBA32);
  check(surface != nullptr, SDL_GetError());
  SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
  check(renderer != nullptr, SDL_GetError());
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  fill(renderer, 0, 0, 100, 100, {0, 0, 0, 255});
  rounded(renderer, 10, 10, 80, 80, 16, {255, 255, 255, 80});
  SDL_RenderPresent(renderer);
  Uint8 r, g, b, alpha;
  const auto *pixel = reinterpret_cast<const Uint32 *>(
      static_cast<const Uint8 *>(surface->pixels) + 50 * surface->pitch);
  SDL_GetRGBA(pixel[50], surface->format, &r, &g, &b, &alpha);
  check(r >= 78 && r <= 81 && g == r && b == r,
        "Translucent panel must blend exactly once");
  SDL_DestroyRenderer(renderer);
  SDL_FreeSurface(surface);

  // A widget centres its label by measuring the ink, not by guessing an offset:
  // a wrong baseline shows up as a few pixels of drift, which is exactly what
  // the old hand-tuned offsets produced.
  SDL_Surface *labelSurface =
      SDL_CreateRGBSurfaceWithFormat(0, 400, 200, 32, SDL_PIXELFORMAT_RGBA32);
  check(labelSurface != nullptr, SDL_GetError());
  SDL_Renderer *labelRenderer = SDL_CreateSoftwareRenderer(labelSurface);
  check(labelRenderer != nullptr, SDL_GetError());
  SDL_SetRenderDrawBlendMode(labelRenderer, SDL_BLENDMODE_BLEND);
  fill(labelRenderer, 0, 0, 400, 200, {0, 0, 0, 255});
  drawButton(labelRenderer, 50, 23, 146, 34, 11, "REFRESH LIST", 1,
             {26, 42, 64, 235}, {58, 84, 112, 90}, {232, 238, 246, 255}, true);
  centeredText(labelRenderer, 200, textCenteredY("REFRESH LIST", 100, 1),
               "REFRESH LIST", 1, {255, 255, 255, 255});
  centeredText(labelRenderer, 200, textCenteredY("刷新列表", 160, 2), "刷新列表",
               2, {255, 255, 255, 255});
  SDL_RenderPresent(labelRenderer);
  const auto lumAt = [&](int x, int y) {
    const auto *row = reinterpret_cast<const Uint32 *>(
        static_cast<const Uint8 *>(labelSurface->pixels) +
        y * labelSurface->pitch);
    Uint8 lr, lg, lb, la;
    SDL_GetRGBA(row[x], labelSurface->format, &lr, &lg, &lb, &la);
    return static_cast<int>(lr) + lg + lb;
  };
  const auto inkCenterY = [&](int x0, int x1, int y0, int y1) {
    int top = -1;
    int bottom = -1;
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x)
        if (lumAt(x, y) > 200) {
          if (top < 0)
            top = y;
          bottom = y;
        }
    return top < 0 ? -1 : (top + bottom) / 2;
  };
  check(std::abs(inkCenterY(60, 190, 20, 62) - 40) <= 1,
        "A button label sits on the button centre");
  check(std::abs(inkCenterY(120, 280, 80, 125) - 100) <= 1,
        "A Latin label is vertically centred");
  check(std::abs(inkCenterY(120, 280, 140, 185) - 160) <= 1,
        "A CJK label is vertically centred");
  int ringPixels = 0;
  for (int x = 30; x < 50; ++x) {
    const int value = lumAt(x, 40);
    if (value >= 650)
      ++ringPixels;
  }
  check(ringPixels > 0, "A focused button draws its focus ring outside the plate");
  SDL_DestroyRenderer(labelRenderer);
  SDL_FreeSurface(labelSurface);
  shutdownTextRenderer();
  SDL_Quit();
  std::cout
      << "Typography, alpha compositing, menu and slot hit targets passed\n";
}
