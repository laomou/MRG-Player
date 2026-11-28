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
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::A)] == SDLK_x,
        "player 1 A is X");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::B)] == SDLK_z,
        "player 1 B is Z");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::TurboA)] ==
            SDLK_c,
        "player 1 turbo A is C");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::TurboB)] ==
            SDLK_v,
        "player 1 turbo B is V");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::X)] == SDLK_s,
        "player 1 X is S");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::Y)] == SDLK_a,
        "player 1 Y is A");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::L)] == SDLK_q,
        "player 1 L is Q");
  check(defaults.keyboard[static_cast<std::size_t>(InputAction::R)] == SDLK_w,
        "player 1 R is W");

  // The keyboard belongs to the first port. A second player is a second pad, so
  // the port past the first reads from an empty table rather than a layout that
  // would have to share the keys player 1 is already holding.
  launcher::config::InputBindings shared = defaults;
  for (std::size_t index = 0; index < kInputActionCount; ++index) {
    check(launcher::config::keyboardBindings(shared, 1)[index] == SDLK_UNKNOWN,
          "player 2 has no key for any action");
  }
  launcher::config::keyboardBindings(shared, 1)
      [static_cast<std::size_t>(InputAction::A)] = SDLK_k;
  check(shared.keyboard[static_cast<std::size_t>(InputAction::A)] == SDLK_x,
        "a write aimed at player 2 never lands on player 1");
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
  check(defaults.controllerPlayer2 == defaults.controller,
        "both ports share the pad layout");

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

  const auto keyboardPrompts = launcher::config::inputPrompts(
      launcher::config::InputDeviceKind::KeyboardMouse,
      launcher::config::ControllerStyle::Auto);
  check(keyboardPrompts.confirm == "ENTER",
        "keyboard actions use the shared confirm key");
  const auto xboxPrompts = launcher::config::inputPrompts(
      launcher::config::InputDeviceKind::Controller,
      launcher::config::ControllerStyle::Xbox);
  check(xboxPrompts.confirm == "A",
        "controller actions use the shared confirm button");
  check(keyboardPrompts.source.fromKeyboard() &&
            !keyboardPrompts.source.fromController(),
        "a keyboard prompt set says it came from the keyboard");
  check(xboxPrompts.source.fromController() &&
            !xboxPrompts.source.fromKeyboard(),
        "a pad prompt set says it came from the pad");
  // The shelf's options drawer sits on the pad's X, so the strip has to name
  // whatever key the player bound there instead of a menu key of its own.
  check(keyboardPrompts.options == "S",
        "the keyboard names its own options key");
  check(xboxPrompts.options == "X", "the pad names its own options button");
  const auto nintendoPrompts = launcher::config::inputPrompts(
      launcher::config::InputDeviceKind::Controller,
      launcher::config::ControllerStyle::Nintendo);
  check(nintendoPrompts.options == "Y",
        "a Nintendo pad puts options on Y");

  // Outside a game the keys standing in for the pad's buttons mean the same
  // thing as the buttons themselves, or a keyboard player cannot reach the
  // shelf's drawer at all.
  const auto launcherKey = [&](SDL_Keycode key) {
    return launcher::config::launcherMenuKey(defaults, key);
  };
  check(launcherKey(SDLK_x) == SDLK_RETURN, "X confirms like the pad's A");
  check(launcherKey(SDLK_RETURN) == SDLK_RETURN, "enter still confirms");
  check(launcherKey(SDLK_z) == SDLK_ESCAPE, "Z backs out like the pad's B");
  check(launcherKey(SDLK_ESCAPE) == SDLK_ESCAPE, "escape still backs out");
  check(launcherKey(SDLK_a) == SDLK_x, "Y opens the drawer with X");
  check(launcherKey(SDLK_s) == SDLK_x, "X's key opens the drawer");
  check(launcherKey(SDLK_UP) == SDLK_UP, "the d-pad keys stay arrows");
  check(launcherKey(SDLK_m) == SDLK_m, "unbound keys reach the screen intact");

  // The controller page gives each column half a row at 1024 px, so the d-pad
  // and the shoulders are drawn as glyphs and abbreviations rather than the
  // long names SDL reports.
  check(launcher::config::keyBindingShortName(SDLK_DOWN) == "\xE2\x86\x93", "the down key is one arrow");
  check(launcher::config::keyBindingShortName(SDLK_RSHIFT) == "RShift", "the right shift key is abbreviated");
  check(launcher::config::controllerButtonShortPrompt(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
                                                      launcher::config::ControllerStyle::Xbox) == "D-RT",
        "the d-pad right button is abbreviated");
  check(launcher::config::controllerButtonShortPrompt(SDL_CONTROLLER_BUTTON_LEFTSTICK,
                                                      launcher::config::ControllerStyle::Xbox) == "LS",
        "the left stick is abbreviated");

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
