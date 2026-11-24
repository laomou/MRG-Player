#include "ui/GameManagementScreen.hpp"
#include "ui/PauseMenuScreen.hpp"
#include "ui/Primitives.hpp"
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
