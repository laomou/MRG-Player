#include "ui/GameControls.hpp"

#include "ui/Primitives.hpp"
#include <algorithm>
#include <array>

namespace launcher::ui {
namespace {
using config::InputAction;
using i18n::TextId;
using i18n::tr;
constexpr std::array<InputAction, 4> kDirections{
    InputAction::Up, InputAction::Down, InputAction::Left, InputAction::Right};

// The bundled CJK font has no U+00B7 glyph. Keep the localized separator
// in the model, but measure a fixed gap and paint a circle instead of tofu.
constexpr const char *kDotSeparator = " · ";
constexpr std::size_t kDotSeparatorBytes = 4;
int separatedTextWidth(const std::string &value, int scale) {
  int width = 0;
  std::size_t begin = 0;
  for (;;) {
    const auto end = value.find(kDotSeparator, begin);
    width += textWidth(value.substr(begin, end - begin), scale);
    if (end == std::string::npos) return width;
    width += 12 * scale;
    begin = end + kDotSeparatorBytes;
  }
}
void drawSeparatedText(SDL_Renderer *renderer, int x, int centerY,
                       const std::string &value, int scale, SDL_Color color) {
  std::size_t begin = 0;
  for (;;) {
    const auto end = value.find(kDotSeparator, begin);
    const auto part = value.substr(begin, end - begin);
    text(renderer, x, textCenteredY(part, centerY, scale), part, scale, color);
    x += textWidth(part, scale);
    if (end == std::string::npos) return;
    circle(renderer, x + 6 * scale, centerY, 2, color);
    x += 12 * scale;
    begin = end + kDotSeparatorBytes;
  }
}

std::string pauseKey(ControlHelpDevice device) {
  if (device == ControlHelpDevice::Brick)
    return "MENU";
  return device == ControlHelpDevice::Keyboard ? "Esc" : "GUIDE";
}
} // namespace

ControlHelp gameControlHelp(game::GamePlatform platform,
                            const config::InputBindings &bindings,
                            ControlHelpDevice device,
                            config::ControllerStyle style,
                            i18n::Language language) {
  ControlHelp help;
  help.source = tr(device == ControlHelpDevice::Keyboard ? TextId::KeyboardKeys
                   : device == ControlHelpDevice::Brick ? TextId::BuiltInControls
                                                        : TextId::ControllerKeys,
                   language);
  help.subtitle = std::string(game::platformDescriptor(platform).displayName) +
                  " · " + tr(TextId::PlayerOne, language);
  // Genesis cores use the RetroPad layout (not same-named console buttons).
  // Keep those labels explicit instead of guessing the core's 3/6-button mode.
  if (platform == game::GamePlatform::Genesis)
    help.subtitle += " · RetroPad";
  help.pauseKey = pauseKey(device);
  const auto binding = [&](InputAction action) -> std::string {
    const auto index = static_cast<std::size_t>(action);
    if (device == ControlHelpDevice::Brick) {
      if (action == InputAction::L) return "L1";
      if (action == InputAction::R) return "R1";
      return config::inputActionName(action);
    }
    if (device == ControlHelpDevice::Keyboard) {
      const SDL_Keycode key = bindings.keyboard[index];
      return key == SDLK_UNKNOWN ? tr(TextId::Unbound, language)
                                : config::keyBindingName(key);
    }
    const auto button = bindings.controller[index];
    if (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ||
        button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
      const bool left = button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
      if (style == config::ControllerStyle::Xbox) return left ? "LB" : "RB";
      if (style == config::ControllerStyle::PlayStation) return left ? "L1" : "R1";
      return left ? "L" : "R";
    }
    return button == SDL_CONTROLLER_BUTTON_INVALID
               ? tr(TextId::Unbound, language)
               : config::controllerButtonPrompt(button, style);
  };
  const auto add = [&](InputAction action) {
    help.rows.push_back({binding(action), config::inputActionName(action)});
  };
  bool standardDirections = true;
  const auto defaults = config::defaultInputBindings();
  for (const auto action : kDirections) {
    const auto i = static_cast<std::size_t>(action);
    if (device == ControlHelpDevice::Keyboard)
      standardDirections &= bindings.keyboard[i] == defaults.keyboard[i];
    else if (device == ControlHelpDevice::Controller)
      standardDirections &= bindings.controller[i] == defaults.controller[i];
  }
  if (standardDirections)
    help.rows.push_back({device == ControlHelpDevice::Keyboard ? "↑ ↓ ← →" : "D-PAD",
                         tr(TextId::Direction, language)});
  else
    for (const auto action : kDirections) add(action);
  add(InputAction::A);
  add(InputAction::B);
  add(InputAction::Start);
  add(InputAction::Select);
  if (platform == game::GamePlatform::Snes || platform == game::GamePlatform::Genesis) {
    add(InputAction::X);
    add(InputAction::Y);
  }
  if (platform == game::GamePlatform::Gba || platform == game::GamePlatform::Snes ||
      platform == game::GamePlatform::Genesis) {
    add(InputAction::L);
    add(InputAction::R);
  }
  return help;
}

ControlHelpLayout controlHelpLayout() {
  const float width = std::min(540.0f, static_cast<float>(kCanvasWidth) - 84);
  const Rect panel{static_cast<float>(kCanvasWidth) - 42 - width, 38, width,
                   static_cast<float>(kCanvasHeight) - 76};
  return {panel,
          {{panel.x + 32, panel.y + 168, panel.w - 64, 30}, kControlHelpRows, 4},
          {panel.right() - 206, panel.bottom() - 58, 174, 38}};
}

int controlHelpPageCount(const ControlHelp &help) {
  return std::max(1, (static_cast<int>(help.rows.size()) + kControlHelpRows - 1) /
                         kControlHelpRows);
}
bool controlHelpBackAt(float x, float y) {
  return controlHelpLayout().back.contains(x, y);
}
bool controlHelpBackEvent(const SDL_Event &event) {
  if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
    const auto key = event.key.keysym.sym;
    return key == SDLK_ESCAPE || key == SDLK_b || key == SDLK_RETURN || key == SDLK_SPACE;
  }
  if (event.type == SDL_CONTROLLERBUTTONDOWN) {
    const auto button = event.cbutton.button;
    return button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_B ||
           button == SDL_CONTROLLER_BUTTON_BACK;
  }
  if (event.type != SDL_MOUSEBUTTONDOWN) return false;
  if (event.button.button == SDL_BUTTON_RIGHT) return true;
  float x = 0, y = 0;
  windowToCanvas(event.button.x, event.button.y, x, y);
  return event.button.button == SDL_BUTTON_LEFT && controlHelpBackAt(x, y);
}
int controlHelpPageStep(const SDL_Event &event) {
  if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
    if (event.key.keysym.sym == SDLK_LEFT || event.key.keysym.sym == SDLK_UP) return -1;
    if (event.key.keysym.sym == SDLK_RIGHT || event.key.keysym.sym == SDLK_DOWN) return 1;
  } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
        event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) return -1;
    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ||
        event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) return 1;
  } else if (event.type == SDL_MOUSEWHEEL && event.wheel.y != 0) {
    return event.wheel.y < 0 ? 1 : -1;
  }
  return 0;
}

void drawControlHelp(SDL_Renderer *renderer, const ControlHelp &help, int page,
                     i18n::Language language, SDL_Color accent) {
  const auto layout = controlHelpLayout();
  const auto &panel = layout.panel;
  const float x = panel.x + 32;
  const float right = panel.right() - 32;
  fill(renderer, 0, 0, kCanvasWidth, kCanvasHeight, {2, 5, 12, 160});
  drawPanel(renderer, panel, 20, {12, 21, 37, 245},
            panel.translated(0, 8), {0, 0, 0, 45});
  text(renderer, x, panel.y + 27, tr(TextId::ControlHelp, language), 3, accent);
  drawSeparatedText(renderer, x, panel.y + 87, help.subtitle, 1,
                    {174, 188, 208, 255});
  fill(renderer, x, panel.y + 119, panel.w - 64, 1, {255, 255, 255, 30});
  text(renderer, x, panel.y + 134, help.source, 1, accent);
  text(renderer, right - 160, panel.y + 134, tr(TextId::GameButtons, language), 1,
       {174, 188, 208, 255});
  const int pages = controlHelpPageCount(help);
  page = std::clamp(page, 0, pages - 1);
  for (int row = 0; row < kControlHelpRows; ++row) {
    const int index = page * kControlHelpRows + row;
    if (index >= static_cast<int>(help.rows.size())) break;
    const auto &entry = help.rows[static_cast<std::size_t>(index)];
    const Rect bounds = layout.rows.item(row);
    const float sourceWidth = bounds.w - 214;
    rounded(renderer, bounds.x, bounds.y, sourceWidth, bounds.h, 7,
            {255, 255, 255, 18});
    const auto label = fitText(entry.binding, static_cast<int>(sourceWidth - 16), 1);
    centeredGlyph(renderer, bounds.x + sourceWidth / 2, bounds.y + bounds.h / 2,
                  label, 1, {240, 244, 250, 255});
    text(renderer, right - 196, bounds.y + 7, "→", 1, {174, 188, 208, 255});
    text(renderer, right - 160, bounds.y + 7, entry.button, 1, {240, 244, 250, 255});
  }
  const float bottom = panel.bottom();
  if (pages > 1)
    text(renderer, x, bottom - 182,
         "←  " + std::to_string(page + 1) + " / " + std::to_string(pages) + "  →",
         1, accent);
  fill(renderer, x, bottom - 155, panel.w - 64, 1, {255, 255, 255, 30});
  text(renderer, x, bottom - 140, help.pauseKey, 2, accent);
  text(renderer, right - 160, bottom - 138, tr(TextId::PauseMenuControl, language),
       1, {240, 244, 250, 255});
  text(renderer, x, bottom - 99,
       fitText(tr(TextId::ControlHelpNote, language), static_cast<int>(panel.w - 64), 1),
       1, {174, 188, 208, 255});
  // The button says BACK and nothing else: a button is its word, and the key
  // glyph on it was the same legend the footer used to carry.
  drawButton(renderer, layout.back, 9, tr(TextId::Back, language), 2, accent,
             {24, 24, 39, 255}, true);
}

float gameEntryHintOpacity(Uint64 elapsedMs) {
  if (elapsedMs >= 3000) return 0.0f;
  if (elapsedMs <= 2400) return 1.0f;
  return static_cast<float>(3000 - elapsedMs) / 600.0f;
}
void drawGameEntryHint(SDL_Renderer *renderer, const std::string &key,
                       Uint64 elapsedMs, i18n::Language language,
                       SDL_Color accent) {
  const float opacity = gameEntryHintOpacity(elapsedMs);
  if (opacity <= 0) return;
  const auto fade = [opacity](SDL_Color c) {
    c.a = static_cast<Uint8>(c.a * opacity);
    return c;
  };
  const std::string label = tr(TextId::PauseEntryHint, language);
  const float keyWidth = static_cast<float>(textWidth(key, 2) + 24);
  const float width = keyWidth + separatedTextWidth(label, 2) + 60;
  const Rect box{(kCanvasWidth - width) / 2, static_cast<float>(kCanvasHeight - 88),
                 width, 58};
  rounded(renderer, box.x, box.y, box.w, box.h, 12, fade({12, 21, 37, 240}));
  rounded(renderer, box.x + 14, box.y + 11, keyWidth, 36, 7, fade(accent));
  centeredGlyph(renderer, box.x + 14 + keyWidth / 2, box.y + box.h / 2, key,
                2, fade({24, 24, 39, 255}));
  drawSeparatedText(renderer, box.x + keyWidth + 32,
                    static_cast<int>(box.y + box.h / 2), label, 2,
                    fade({240, 244, 250, 255}));
}

} // namespace launcher::ui
