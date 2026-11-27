#include "ui/GameControls.hpp"
#include "ui/Primitives.hpp"
#include "ui/Theme.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>

namespace {
void check(bool result, const char *message) {
  if (!result) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}
}

// Called before the reference-only checks so both native canvases exercise
// the full model, input handling, layout and actual SDL rendering.
void testGameControls(const char *fontPath) {
  using namespace launcher;
  using namespace ui;
  using config::InputAction;
  using config::ControllerStyle;
  using game::GamePlatform;
  using i18n::Language;
  auto bindings = config::defaultInputBindings();
  const auto help = [&](GamePlatform platform, ControlHelpDevice device,
                        ControllerStyle style = ControllerStyle::Xbox) {
    return gameControlHelp(platform, bindings, device, style, Language::English);
  };
  auto nes = help(GamePlatform::Nes, ControlHelpDevice::Keyboard);
  check(nes.rows.size() == 5 && nes.rows[0].binding == "↑ ↓ ← →" &&
            nes.rows[1].binding == "X" && nes.rows[1].button == "A" &&
            nes.rows[2].binding == "Z" && nes.rows[2].button == "B" &&
            nes.pauseKey == "Esc" && controlHelpPageCount(nes) == 1,
        "NES shows only the five basic rows and keyboard pause");
  check(help(GamePlatform::GameBoy, ControlHelpDevice::Keyboard).rows.size() == 5,
        "GB excludes unused shoulders and extra face buttons");
  auto gba = help(GamePlatform::Gba, ControlHelpDevice::Brick);
  check(gba.rows.size() == 7 && gba.rows[1].binding == "A" &&
            gba.rows[2].binding == "B" && gba.rows[5].binding == "L1" &&
            gba.rows[6].binding == "R1" && gba.pauseKey == "MENU" &&
            controlHelpPageCount(gba) == 1,
        "Brick GBA uses physical A/B/L1/R1 and MENU, with no keyboard hints");
  auto snes = help(GamePlatform::Snes, ControlHelpDevice::Controller);
  check(snes.rows.size() == 9 && controlHelpPageCount(snes) == 2 &&
            snes.pauseKey == "GUIDE",
        "Wider pads paginate rather than overflowing the drawer");
  check(help(GamePlatform::Genesis, ControlHelpDevice::Keyboard).subtitle.find(
            "RetroPad") != std::string::npos,
        "Genesis explicitly names the core input layout");
  const auto a = static_cast<std::size_t>(InputAction::A);
  bindings.keyboard[a] = SDLK_k;
  bindings.keyboardPlayer2[a] = SDLK_p;
  bindings.controller[a] = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
  check(help(GamePlatform::Nes, ControlHelpDevice::Keyboard).rows[1].binding == "K" &&
            help(GamePlatform::Nes, ControlHelpDevice::Controller).rows[1].binding == "RB" &&
            help(GamePlatform::Nes, ControlHelpDevice::Controller,
                 ControllerStyle::Nintendo).rows[1].binding == "R" &&
            help(GamePlatform::Nes, ControlHelpDevice::Controller,
                 ControllerStyle::PlayStation).rows[1].binding == "R1" &&
            help(GamePlatform::Nes, ControlHelpDevice::Brick).rows[1].binding == "A",
        "Help reads current P1 bindings and pad style; Brick keeps its fixed raw mapping");
  bindings.keyboard[a] = SDLK_UNKNOWN;
  bindings.controller[a] = SDL_CONTROLLER_BUTTON_INVALID;
  check(help(GamePlatform::Nes, ControlHelpDevice::Keyboard).rows[1].binding == "UNBOUND" &&
            help(GamePlatform::Nes, ControlHelpDevice::Controller).rows[1].binding == "UNBOUND",
        "Unbound inputs are not advertised as real keys");
  bindings.keyboard[static_cast<std::size_t>(InputAction::Up)] = SDLK_w;
  bindings.controller[static_cast<std::size_t>(InputAction::Up)] = SDL_CONTROLLER_BUTTON_Y;
  auto custom = help(GamePlatform::Nes, ControlHelpDevice::Keyboard);
  check(custom.rows.size() == 8 && custom.rows[0].binding == "W" &&
            controlHelpPageCount(custom) == 2 &&
            help(GamePlatform::Nes, ControlHelpDevice::Controller).rows[0].binding == "Y",
        "Custom directions are expanded, not mislabelled as the default dpad");

  check(gameEntryHintOpacity(0) == 1 && gameEntryHintOpacity(2400) == 1 &&
            gameEntryHintOpacity(2700) == 0.5f && gameEntryHintOpacity(2999) > 0 &&
            gameEntryHintOpacity(3000) == 0 &&
            gameEntryHintOpacity(std::numeric_limits<Uint64>::max()) == 0,
        "Entry hint holds then fades and stays gone at three seconds");
  const auto layout = controlHelpLayout();
  check(layout.panel.x >= 0 && layout.panel.right() <= kCanvasWidth &&
            layout.panel.bottom() <= kCanvasHeight &&
            layout.rows.item(6).bottom() < layout.panel.bottom() - 182 &&
            layout.back.bottom() < layout.panel.bottom() &&
            controlHelpBackAt(layout.back.centerX(), layout.back.centerY()) &&
            !controlHelpBackAt(layout.back.right(), layout.back.centerY()),
        "Help rows, pagination and Back fit and share half-open hit geometry");
  setOutputSize(kCanvasWidth, kCanvasHeight);
  setWindowSize(0, 0);
  SDL_Event event{};
  event.type = SDL_KEYDOWN;
  for (const auto key : {SDLK_RETURN, SDLK_ESCAPE, SDLK_SPACE, SDLK_b}) {
    event.key.keysym.sym = key;
    check(controlHelpBackEvent(event), "Keyboard confirm/back closes help");
  }
  event.key.repeat = 1;
  check(!controlHelpBackEvent(event), "Opening key repeat cannot immediately close help");
  event.key.repeat = 0;
  event.key.keysym.sym = SDLK_RIGHT;
  check(!controlHelpBackEvent(event) && controlHelpPageStep(event) == 1,
        "Directions page help instead of dismissing it");
  event.type = SDL_CONTROLLERBUTTONDOWN;
  event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
  check(controlHelpBackEvent(event), "Controller confirm activates focused Back");
  event.cbutton.button = SDL_CONTROLLER_BUTTON_B;
  check(controlHelpBackEvent(event), "Controller cancel closes help");
  event.cbutton.button = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
  check(controlHelpPageStep(event) == -1, "Dpad pages help");
  event.type = SDL_MOUSEBUTTONDOWN;
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = static_cast<int>(layout.back.centerX());
  event.button.y = static_cast<int>(layout.back.centerY());
  check(controlHelpBackEvent(event), "Back button pointer target matches painting");
  event.button.x = 0;
  check(!controlHelpBackEvent(event), "Clicking the game does not activate Back");

  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
      0, kCanvasWidth, kCanvasHeight, 32, SDL_PIXELFORMAT_RGBA32);
  check(surface != nullptr, SDL_GetError());
  SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
  check(renderer != nullptr, SDL_GetError());
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  const auto pixel = [&](int x, int y) {
    SDL_RenderPresent(renderer);
    const auto *row = reinterpret_cast<const Uint32 *>(
        static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch);
    SDL_Color color{};
    SDL_GetRGBA(row[x], surface->format, &color.r, &color.g, &color.b, &color.a);
    return color;
  };
  const auto clear = [&] { fill(renderer, 0, 0, kCanvasWidth, kCanvasHeight, {0, 0, 0, 255}); };
  const auto snapshot = [&](const std::string &name) {
    if (const char *dir = std::getenv("MRG_UI_SNAPSHOT_DIR")) {
      std::filesystem::create_directories(dir);
      const auto path = std::filesystem::path(dir) /
          (name + "-" + std::to_string(kCanvasWidth) + ".bmp");
      SDL_RenderPresent(renderer);
      check(SDL_SaveBMP(surface, path.string().c_str()) == 0, "Save UI snapshot");
    }
  };
  // The CJK face carries no arrow glyphs. A missing glyph comes back as a
  // hollow box, whose middle column holds ink only on its two borders, while a
  // real arrow has a shaft running down the middle.
  clear();
  text(renderer, 40, 40, "↑", 3, {255, 255, 255, 255});
  SDL_RenderPresent(renderer);
  const auto inked = [&](int x, int y) {
    const auto *row = reinterpret_cast<const Uint32 *>(
        static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch);
    SDL_Color ink{};
    SDL_GetRGBA(row[x], surface->format, &ink.r, &ink.g, &ink.b, &ink.a);
    return ink.r > 180 && ink.g > 180 && ink.b > 180;
  };
  int left = 120, right = -1, top = 120, bottom = -1;
  for (int y = 0; y < 120; ++y)
    for (int x = 0; x < 120; ++x)
      if (inked(x, y)) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
  check(right > left && bottom > top, "Arrow glyph painted something");
  int middle = 0;
  for (int y = top; y <= bottom; ++y)
    middle += inked((left + right) / 2, y) ? 1 : 0;
  check(middle * 2 >= bottom - top,
        "Arrows come from a face that has them instead of an empty box");

  bindings = config::defaultInputBindings();
  for (const auto language : {Language::English, Language::SimplifiedChinese}) {
    for (int theme = 0; theme < kAccentCount; ++theme) {
      for (const auto device : {ControlHelpDevice::Keyboard, ControlHelpDevice::Controller,
                                ControlHelpDevice::Brick}) {
        const auto model = gameControlHelp(GamePlatform::Gba, bindings, device,
                                          ControllerStyle::Xbox, language);
        clear();
        const auto accent = accentColor(theme);
        drawControlHelp(renderer, model, 99, language, accent);
        auto p = pixel(static_cast<int>(layout.back.x + 12), static_cast<int>(layout.back.centerY()));
        check(p.r == accent.r && p.g == accent.g && p.b == accent.b,
              "Back follows every theme accent");
        p = pixel(static_cast<int>(layout.back.centerX()), static_cast<int>(layout.back.y - 3));
        check(p.r == kFocusColor.r && p.g == kFocusColor.g && p.b == kFocusColor.b,
              "Help Back carries the shared white focus ring");
        const int subtitleDotX = static_cast<int>(layout.panel.x + 32) +
            textWidth(std::string(game::platformDescriptor(GamePlatform::Gba).displayName), 1) + 6;
        const int subtitleDotY = static_cast<int>(layout.panel.y + 87);
        p = pixel(subtitleDotX, subtitleDotY);
        check(p.r == 174 && p.g == 188 && p.b == 208 &&
                  pixel(subtitleDotX, subtitleDotY - 4).r < 50,
              "Subtitle separator is a painted dot, not a missing-glyph box");
        if (theme == 0 && language == Language::SimplifiedChinese)
          snapshot("controls-" + std::to_string(static_cast<int>(device)));
        clear();
        drawGameEntryHint(renderer, model.pauseKey, 0, language, accent);
        check(pixel(kCanvasWidth / 2, kCanvasHeight - 84).b > 0,
              "Entry strip renders at native canvas coordinates");
        check(pixel(0, 0).b == 0, "Entry strip leaves the game outside it untouched");
        const std::string label = i18n::tr(i18n::TextId::PauseEntryHint, language);
        const auto separator = label.find(" · ");
        check(separator != std::string::npos, "Hint contains a separator");
        const int beforeWidth = textWidth(label.substr(0, separator), 2);
        const int afterWidth = textWidth(label.substr(separator + 4), 2);
        const int keyWidth = textWidth(model.pauseKey, 2) + 24;
        const int boxWidth = keyWidth + beforeWidth + 24 + afterWidth + 60;
        const int dotX = static_cast<int>((kCanvasWidth - boxWidth) / 2.0f +
                                         keyWidth + 32) + beforeWidth + 12;
        const int dotY = kCanvasHeight - 59;
        p = pixel(dotX, dotY);
        check(p.r == 240 && p.g == 244 && p.b == 250 &&
                  pixel(dotX - 4, dotY - 4).r < 50 &&
                  pixel(dotX + 4, dotY + 4).r < 50,
              "Entry separator paints a centered circle without a tofu box");
        if (theme == 0 && language == Language::SimplifiedChinese)
          snapshot("entry-" + std::to_string(static_cast<int>(device)));
        clear();
        drawGameEntryHint(renderer, model.pauseKey, 2700, language, accent);
        p = pixel(dotX, dotY);
        check(p.r > 80 && p.r < 160, "Separator fades with the rest of the hint");
        clear();
        drawGameEntryHint(renderer, model.pauseKey, 3000, language, accent);
        check(pixel(kCanvasWidth / 2, kCanvasHeight - 84).b == 0,
              "Expired entry strip draws nothing");
      }
    }
  }
  clear();
  drawControlHelp(renderer, custom, 1, Language::English, accentColor(0));
  snapshot("controls-custom-page2");
  // Clear cached text textures before their renderer is destroyed.
  shutdownTextRenderer();
  SDL_DestroyRenderer(renderer);
  SDL_FreeSurface(surface);
  check(initializeTextRenderer(fontPath), "Restore font renderer after control tests");
}
