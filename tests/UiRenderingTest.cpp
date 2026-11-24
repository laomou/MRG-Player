#include "ui/GameManagementScreen.hpp"
#include "ui/HomeScreen.hpp"
#include "ui/PauseMenuScreen.hpp"
#include "ui/Primitives.hpp"
#include "ui/SettingsScreen.hpp"
#include <SDL.h>
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
  check(homePillAt(1200, 80) == 0, "Library pill target preserved");
  check(homePillAt(1450, 80) == 1, "Settings pill target preserved");
  check(homePillAt(700, 80) == -1, "Empty header space activates nothing");
  check(homeCardAt(500, 500, 7, 0.0f) >= 0,
        "A card in the row is clickable");
  check(homeCardAt(500, 200, 7, 0.0f) == -1,
        "Above the row activates nothing");
  // Seven cards fit, so the row only travels once the library outgrows them.
  check(homeRowTarget(0, 5) == 0.0f && homeRowTarget(6, 7) == 0.0f,
        "A short library never scrolls");
  check(homeRowTarget(8, 10) == 2.0f && homeRowTarget(9, 10) == 3.0f,
        "A long library keeps the selection on the last visible column");
  check(homeCardAt(1500, 500, 10, 2.0f) >= 7,
        "Scrolling the row moves the cards under the pointer");
  launcher::config::InputPrompts prompts;
  check(homeActionAt(1500, 828, true, launcher::i18n::Language::English,
                     prompts) == 2,
        "The last footer entry is the rightmost button");
  check(homeActionAt(700, 828, true, launcher::i18n::Language::English,
                     prompts) == -1,
        "Empty footer space activates nothing");
  check(homeActionCount(true) == 3 && homeActionCount(false) == 1,
        "An empty slot only offers to quit");

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
