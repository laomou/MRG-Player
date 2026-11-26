#include "input/BrickInput.hpp"

#include <cassert>
#include <iostream>

using launcher::config::InputAction;
using launcher::input::BrickInputAdapter;
using launcher::input::BrickUiCommand;

int main() {
  using namespace launcher::input;

  assert(brickButtonAction(kBrickButtonA) == InputAction::A);
  assert(brickButtonAction(kBrickButtonB) == InputAction::B);
  assert(brickButtonAction(kBrickButtonX) == InputAction::X);
  assert(brickButtonAction(kBrickButtonY) == InputAction::Y);
  assert(brickButtonAction(kBrickButtonL1) == InputAction::L);
  assert(brickButtonAction(kBrickButtonR1) == InputAction::R);
  assert(!brickButtonAction(kBrickButtonMenu));
  assert(brickAxis(0) == SDL_CONTROLLER_AXIS_LEFTX);
  assert(brickAxis(1) == SDL_CONTROLLER_AXIS_LEFTY);
  assert(!brickAxis(2));

  BrickInputAdapter adapter;
  SDL_Event source{};
  source.type = SDL_JOYBUTTONDOWN;
  source.jbutton.which = 42;
  source.jbutton.state = SDL_PRESSED;
  source.jbutton.button = kBrickButtonA;
  auto translated = adapter.translateForUi(source);
  assert(translated.consumed && translated.hasEvent);
  assert(translated.event.type == SDL_CONTROLLERBUTTONDOWN);
  assert(translated.event.cbutton.which == 42);
  assert(translated.event.cbutton.button == SDL_CONTROLLER_BUTTON_A);

  source.jbutton.button = kBrickButtonB;
  translated = adapter.translateForUi(source);
  assert(translated.event.cbutton.button == SDL_CONTROLLER_BUTTON_B);
  source.jbutton.button = kBrickButtonX;
  translated = adapter.translateForUi(source);
  assert(translated.event.cbutton.button == SDL_CONTROLLER_BUTTON_X);
  source.jbutton.button = kBrickButtonY;
  translated = adapter.translateForUi(source);
  assert(translated.event.cbutton.button == SDL_CONTROLLER_BUTTON_Y);

  source.jbutton.button = kBrickButtonMenu;
  translated = adapter.translateForUi(source);
  assert(translated.event.cbutton.button == SDL_CONTROLLER_BUTTON_B);
  assert(brickMenuPressed(source));

  source.jbutton.button = kBrickButtonVolumeUp;
  translated = adapter.translateForUi(source);
  assert(translated.consumed && !translated.hasEvent);
  assert(translated.command == BrickUiCommand::VolumeUp);

  source = {};
  source.type = SDL_JOYAXISMOTION;
  source.jaxis.which = 42;
  source.jaxis.axis = 0;
  source.jaxis.value = 25000;
  translated = adapter.translateForUi(source);
  assert(translated.hasEvent);
  assert(translated.event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  assert(!adapter.translateForUi(source).hasEvent);
  source.jaxis.value = 0;
  assert(!adapter.translateForUi(source).hasEvent);
  source.jaxis.value = -25000;
  translated = adapter.translateForUi(source);
  assert(translated.hasEvent);
  assert(translated.event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT);

  std::cout << "Brick raw button and UI translations passed\n";
  return 0;
}
