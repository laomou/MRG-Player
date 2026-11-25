#include "config/InputBindings.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string &what) {
  if (condition)
    return;
  std::fprintf(stderr, "FAIL: %s\n", what.c_str());
  ++failures;
}

} // namespace

int main() {
  using launcher::config::InputAction;
  using launcher::config::kInputActionCount;
  using launcher::config::kPlayerCount;

  check(kInputActionCount == 14, "fourteen bindable actions");
  check(kPlayerCount == 2, "two controller ports");

  // The eight d-pad and classic buttons keep their slots, the turbo pair keeps
  // the slots it always had, and the extra face and shoulder buttons follow —
  // appending only, so a saved settings.ini still means what it meant.
  check(static_cast<std::size_t>(InputAction::Right) == 7,
        "right closes the classic button block");
  check(static_cast<std::size_t>(InputAction::TurboA) == 8,
        "turbo A keeps its slot");
  check(static_cast<std::size_t>(InputAction::TurboB) == 9,
        "turbo B keeps its slot");
  check(static_cast<std::size_t>(InputAction::X) == 10,
        "X follows the turbo pair");
  check(static_cast<std::size_t>(InputAction::R) == kInputActionCount - 1,
        "R is the last action");

  const launcher::config::InputBindings defaults =
      launcher::config::defaultInputBindings();
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::A)] == SDLK_z,
        "player 1 A is Z");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::TurboA)] ==
            SDLK_c,
        "player 1 turbo A is C");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::TurboB)] ==
            SDLK_v,
        "player 1 turbo B is V");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::X)] == SDLK_f,
        "player 1 X is F");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::Y)] == SDLK_j,
        "player 1 Y is J");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::L)] == SDLK_q,
        "player 1 L is Q");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::R)] == SDLK_e,
        "player 1 R is E");
  // Turbo moved to the stick clicks: a pad that presses Y or B for a turbo
  // pulse could never press the real Y or B at the same time.
  check(defaults.controller[static_cast<std::size_t>(InputAction::TurboA)] ==
            SDL_CONTROLLER_BUTTON_LEFTSTICK,
        "pad turbo A is the left stick click");
  check(defaults.controller[static_cast<std::size_t>(InputAction::TurboB)] ==
            SDL_CONTROLLER_BUTTON_RIGHTSTICK,
        "pad turbo B is the right stick click");
  check(defaults.controller[static_cast<std::size_t>(InputAction::X)] ==
            SDL_CONTROLLER_BUTTON_Y,
        "pad X is the Y button, as on a Super Nintendo pad");
  check(defaults.controller[static_cast<std::size_t>(InputAction::Y)] ==
            SDL_CONTROLLER_BUTTON_B,
        "pad Y is the B button");
  check(defaults.controller[static_cast<std::size_t>(InputAction::L)] ==
            SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
        "pad L is the left shoulder");
  check(defaults.controller[static_cast<std::size_t>(InputAction::R)] ==
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        "pad R is the right shoulder");
  check(defaults.keyboardPlayer2[static_cast<std::size_t>(InputAction::TurboA)] ==
            SDLK_b,
        "player 2 turbo A is B");
  check(defaults.keyboardPlayer2[static_cast<std::size_t>(InputAction::X)] ==
            SDLK_i,
        "player 2 X is I");
  // Player 2 must not collide with the keys player 1 uses for the same console
  // buttons, otherwise one keypress drives both ports.
  for (std::size_t index = 0; index < kInputActionCount; ++index) {
    check(defaults.keyboard[index] != defaults.keyboardPlayer2[index],
          "player 2 keys are distinct from player 1");
  }

  check(std::string(launcher::config::inputActionName(InputAction::TurboA)) ==
            "TURBO A",
        "turbo A is named");
  check(std::string(launcher::config::inputActionName(InputAction::TurboB)) ==
            "TURBO B",
        "turbo B is named");
  check(std::string(launcher::config::inputActionName(InputAction::X)) == "X",
        "X is named");
  check(std::string(launcher::config::inputActionName(InputAction::R)) == "R",
        "R is named");

  // The pulse is a 50% duty cycle: two frames held, two frames released. Half a
  // second of NTSC frames therefore presses exactly fifteen times.
  int presses = 0;
  for (std::uint64_t frame = 0; frame < 30; ++frame) {
    const bool on = launcher::config::turboPulseOn(frame);
    if (frame < 2 || (frame >= 4 && frame < 6))
      check(on, "pulse is on at the start of each period");
    if (frame >= 2 && frame < 4)
      check(!on, "pulse is off in the second half");
    if (on && (frame == 0 || !launcher::config::turboPulseOn(frame - 1)))
      ++presses;
  }
  check(presses == 8, "half a second holds eight presses at 15 per second");

  if (failures != 0) {
    std::fprintf(stderr, "%d input binding check(s) failed\n", failures);
    return 1;
  }
  std::printf("input-binding checks passed\n");
  return 0;
}
