#include "ui/GameManagementScreen.hpp"
#include "ui/HomeScreen.hpp"
#include "ui/PauseMenuScreen.hpp"
#include "ui/Primitives.hpp"
#include "ui/SettingsScreen.hpp"
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
    check(generalPageRowAt(1200, 404 + row * 84 + 20) == row,
          "General row hit target");
  check(generalPageRowAt(1200, 404 + settingRowCount(0) * 84) == -1,
        "General hit testing must stop after the last row");
  check(generalPageRowAt(300, 420) == -1,
        "The settings sidebar must not activate general rows");
  // Every clickable control on the settings screen answers a click: the
  // category rail and the rows of each page.
  check(settingsCategoryAt(200, 389 + 2 * 52 + 10) == 2,
        "The category rail maps a click to its entry");
  check(settingsCategoryAt(200, 770) == -1,
        "Below the rail activates nothing");
  check(settingsCategoryAt(500, 420) == -1,
        "The settings body must not activate categories");
  check(settingsRowAt(1, 1200, 728 + 10) == 3,
        "Every display row is clickable, not just the first");
  check(settingsRowAt(1, 1200, 728 + 100) == -1,
        "The gap under the last display row activates nothing");
  check(settingsRowAt(2, 1200, 516 + 10) == 1,
        "Audio rows map to their own index");
  check(settingsRowAt(4, 300, 420) == -1,
        "Outside the row track activates nothing");
  check(settingsRowAt(6, 1200, 420) == 0, "The about row is clickable");
  check(powerCycleChoiceAt(600, 530) == 0,
        "Power-cycle dialog defaults to cancel");
  check(powerCycleChoiceAt(900, 530) == 1, "Power-cycle confirmation target");
  check(powerCycleChoiceAt(600, 300) == -1, "Dialog text must not confirm");
  for (int action = 0; action < kManagementActionCount; ++action)
    check(managementActionAt(1200, 288 + action * 80 + (action >= 4 ? 18 : 0) +
                                       34) == action,
          "Management hit target must follow display order");
  check(managementActionAt(1200, 612) == -1,
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
  check(settingsCategoryAt(200, 389 + 2 * 52 + 10) == 2,
        "The category rail hit target follows the list");
  check(settingsRowAt(1, 1200, 410 + 2 * 106 + 20) == 2,
        "The display page rows are all clickable");

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
  setOutputSize(1600, 900);

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
  shutdownTextRenderer();
  SDL_Quit();
  std::cout
      << "Typography, alpha compositing, menu and slot hit targets passed\n";
}
