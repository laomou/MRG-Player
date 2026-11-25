#include "ui/SettingsScreen.hpp"

#include "config/InputBindings.hpp"
#include "i18n/Localization.hpp"
#include "model/Game.hpp"
#include "ui/Primitives.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

// CMake passes the project version in; a build that bypasses it still reports
// something instead of an empty row.
#ifndef MRG_VERSION
#define MRG_VERSION "development"
#endif

namespace launcher::ui {
namespace {
using i18n::Language;
using i18n::TextId;
using i18n::tr;

constexpr int kCategoryCount = 8;

// Geometry of the dense controller page, shared by drawing and hit testing.
constexpr int kCompactStartY = 400;
constexpr int kCompactStep = 30;
constexpr int kRowHeight = 29;
constexpr int kTrackX = 466;
constexpr int kTrackWidth = 1010;
constexpr int kFixedRowCount = 4;
constexpr int kColumnGutter = 16;
constexpr int kColumnWidth = (kTrackWidth - kColumnGutter) / 2;
constexpr int kRightColumnX = kTrackX + kColumnWidth + kColumnGutter;
// Seven rows per column: the pad actions (A/B/X/Y/L/R with select and start)
// plus the two turbo rows, with the dead-zone slider under both columns.
constexpr std::size_t kBindingsPerColumn = 7;
static_assert(config::kInputActionCount == 2 * kBindingsPerColumn);

// The general page carries five rows now, so its rows are slightly shorter
// than the ones on the other pages.
constexpr int kGeneralRowCount = 5;
constexpr int kGeneralRowY = 404;
constexpr int kGeneralRowStep = 84;
constexpr int kGeneralRowHeight = 76;

// The single-column pages use 92 pixel rows on a 106 pixel pitch, and the
// installer page lists one platform per 56 pixel line.
constexpr int kSettingRowY = 410;
constexpr int kSettingRowStep = 106;
constexpr int kSettingRowHeight = 92;
constexpr int kInstallerRowCount = 5;

// The core page gives every platform one 48 pixel line on a 56 pixel pitch,
// with the installed counter and the refresh button on the heading line. The
// columns are fixed so the platform name, the core file, and the status never
// wander between rows.
constexpr int kCoreRowY = 418;
constexpr int kCoreRowStep = 56;
constexpr int kCoreRowHeight = 48;
constexpr int kCoreRefreshX = 1330;
constexpr int kCoreRefreshY = 331;
constexpr int kCoreRefreshWidth = 146;
constexpr int kCoreRefreshHeight = 34;
constexpr int kCoreColumnCoreX = 900;
constexpr int kCorePillRight = 1450;

TextId categoryText(int category) {
  constexpr std::array<TextId, kCategoryCount> ids{
      TextId::General,    TextId::Display,     TextId::Audio,
      TextId::Controller, TextId::GameLibrary, TextId::Cores,
      TextId::Theme,      TextId::About};
  return ids[static_cast<std::size_t>(
      std::clamp(category, 0, kCategoryCount - 1))];
}

TextId categoryDescription(int category) {
  constexpr std::array<TextId, kCategoryCount> ids{
      TextId::GeneralDescription, TextId::DisplayDescription,
      TextId::AudioDescription,   TextId::ControllerDescription,
      TextId::LibraryDescription, TextId::CoresDescription,
      TextId::ThemeDescription,   TextId::AboutDescription};
  return ids[static_cast<std::size_t>(
      std::clamp(category, 0, kCategoryCount - 1))];
}

void drawSettingRow(SDL_Renderer *renderer, int y, bool selected,
                    bool focused, const std::string &title,
                    const std::string &description, const std::string &value,
                    SDL_Color accent, int height = 92,
                    float focusOpacity = 1.0f) {
  // The wash marks the row the keys are acting on, exactly like the tint on the
  // category rail, and the ring only appears when the keys are actually in the
  // page. The ring goes on last so it stays a line rather than a wash over the
  // plate.
  const SDL_Color plate{7, 17, 34, 188};
  rounded(renderer, 466, static_cast<float>(y), 1010,
          static_cast<float>(height), 17, plate);
  if (selected) {
    rounded(renderer, 466, static_cast<float>(y), 1010,
            static_cast<float>(height), 17,
            {accent.r, accent.g, accent.b, accentWashAlpha(accent)});
  }
  // A row without a description puts the label itself on the centre line, so the
  // page keeps one rhythm instead of a ragged column of top-aligned labels.
  const bool hasDescription = !description.empty();
  const int titleY = hasDescription ? y + (height - 58) / 2 : y + (height - 34) / 2;
  text(renderer, 498, titleY, title, 2,
       selected || focused ? SDL_Color{255, 255, 255, 255}
                           : SDL_Color{247, 248, 252, 255});
  if (hasDescription)
    text(renderer, 498, titleY + 34, description, 1, {148, 169, 187, 255});
  const int valueX = 1420 - textWidth(value, 2);
  text(renderer, valueX, y + (height - 24) / 2, value, 2,
       selected || focused ? accent : SDL_Color{205, 215, 226, 255});
  if (focused)
    drawFocusRing(renderer, 466, static_cast<float>(y), 1010,
                  static_cast<float>(height), 17, focusOpacity);
}

// One dense line of the controller page. It takes a rectangle so the binding
// list can be split into two columns without a second implementation.
void drawCompactRow(SDL_Renderer *renderer, int x, int y, int width,
                    bool selected, bool focused, const std::string &title,
                    const std::string &value, SDL_Color accent,
                    float focusOpacity = 1.0f) {
  const SDL_Color plate{7, 17, 34, 170};
  rounded(renderer, static_cast<float>(x), static_cast<float>(y),
          static_cast<float>(width), 29, 9, plate);
  if (selected) {
    rounded(renderer, static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(width), 29, 9,
            {accent.r, accent.g, accent.b, accentWashAlpha(accent)});
  }
  text(renderer, x + 26, y + 4, title, 1,
       selected || focused ? SDL_Color{255, 255, 255, 255}
                           : SDL_Color{184, 198, 216, 255});
  const int valueX = x + width - 20 - textWidth(value, 1);
  text(renderer, valueX, y + 4, value, 1,
       selected || focused ? accent : SDL_Color{205, 215, 226, 255});
  if (focused)
    drawFocusRing(renderer, static_cast<float>(x), static_cast<float>(y),
                  static_cast<float>(width), 29, 9, focusOpacity);
}

TextId aspectText(int aspect) {
  if (aspect == 1)
    return TextId::AspectScreen4x3;
  if (aspect == 2)
    return TextId::AspectPixels8x7;
  return TextId::AspectPixel;
}

TextId accentText(int accent) {
  constexpr std::array<TextId, kAccentCount> ids{
      TextId::AccentTeal, TextId::AccentBlue, TextId::AccentPurple,
      TextId::AccentLime, TextId::AccentOrange, TextId::AccentYellow};
  return ids[static_cast<std::size_t>(
      std::clamp(accent, 0, kAccentCount - 1))];
}

TextId promptDeviceText(config::PromptDeviceMode mode) {
  if (mode == config::PromptDeviceMode::KeyboardMouse)
    return TextId::KeyboardMouse;
  if (mode == config::PromptDeviceMode::Controller)
    return TextId::ControllerPrompts;
  return TextId::Automatic;
}

TextId controllerStyleText(config::ControllerStyle style) {
  switch (style) {
  case config::ControllerStyle::Xbox:
    return TextId::Xbox;
  case config::ControllerStyle::Nintendo:
    return TextId::NintendoSwitch;
  case config::ControllerStyle::PlayStation:
    return TextId::PlayStation;
  case config::ControllerStyle::Generic:
    return TextId::Generic;
  default:
    return TextId::Automatic;
  }
}

} // namespace

int settingCategoryCount() { return kCategoryCount; }

int settingRowCount(int category) {
  if (category == 0)
    return kGeneralRowCount;
  if (category == 1)
    return 4;
  if (category == 2)
    return 3;
  if (category == 3)
    return 4 + static_cast<int>(config::kInputActionCount) + 1;
  if (category == 5)
    return 5;
  return 1;
}

int controllerPageRowAt(int x, int y) {
  if (x < kTrackX || x > kTrackX + kTrackWidth)
    return -1;
  if (y < kCompactStartY)
    return -1;
  const int line = (y - kCompactStartY) / kCompactStep;
  if (line < 0 || line >= kFixedRowCount + static_cast<int>(kBindingsPerColumn) +
                              1)
    return -1;
  if ((y - kCompactStartY) % kCompactStep >= kRowHeight && line >= kFixedRowCount)
    return -1;
  if (line < kFixedRowCount)
    return line;
  if (line < kFixedRowCount + static_cast<int>(kBindingsPerColumn)) {
    // The two binding columns only cover their own half of the track.
    const bool right = x >= kRightColumnX - kColumnGutter / 2;
    if (right && x < kRightColumnX)
      return -1;
    const int column = right ? 1 : 0;
    return kFixedRowCount + column * static_cast<int>(kBindingsPerColumn) +
           (line - kFixedRowCount);
  }
  return kFixedRowCount + static_cast<int>(config::kInputActionCount);
}

int generalPageRowAt(int x, int y) {
  if (x < kTrackX || x > kTrackX + kTrackWidth)
    return -1;
  if (y < kGeneralRowY)
    return -1;
  const int row = (y - kGeneralRowY) / kGeneralRowStep;
  if (row < 0 || row >= kGeneralRowCount)
    return -1;
  if ((y - kGeneralRowY) % kGeneralRowStep >= kGeneralRowHeight)
    return -1;
  return row;
}

int settingsCategoryAt(float x, float y) {
  if (x < 72 || x > 402 || y < 389 || y >= 389 + kCategoryCount * 52)
    return -1;
  const int category = static_cast<int>((y - 389) / 52);
  return category < kCategoryCount ? category : -1;
}

int settingsRowAt(int category, float x, float y) {
  if (x < kTrackX || x > kTrackX + kTrackWidth)
    return -1;
  if (category == 0)
    return generalPageRowAt(static_cast<int>(x), static_cast<int>(y));
  if (category == 3)
    return controllerPageRowAt(static_cast<int>(x), static_cast<int>(y));
  if (category == 5) {
    if (y < kCoreRowY)
      return -1;
    const int row = (static_cast<int>(y) - kCoreRowY) / kCoreRowStep;
    if (row < 0 || row >= kInstallerRowCount)
      return -1;
    if ((static_cast<int>(y) - kCoreRowY) % kCoreRowStep >= kCoreRowHeight)
      return -1;
    return row;
  }
  // The remaining pages share one column of 92 pixel rows spaced 106 apart.
  const int row = (static_cast<int>(y) - kSettingRowY) / kSettingRowStep;
  const int count = category == 1 ? 4 : (category == 2 ? 3 : 1);
  if (row < 0 || row >= count)
    return -1;
  if ((static_cast<int>(y) - kSettingRowY) % kSettingRowStep >=
      kSettingRowHeight)
    return -1;
  return row;
}

bool coreRefreshAt(float x, float y) {
  // Re-reading the disk is the one control that is not a list row, so it gets
  // its own rectangle instead of a row index.
  return x >= kCoreRefreshX && x <= kCoreRefreshX + kCoreRefreshWidth &&
         y >= kCoreRefreshY && y <= kCoreRefreshY + kCoreRefreshHeight;
}

void changeSetting(AppSettings &settings, int category, int row,
                   int direction) {
  if (category == 0) {
    if (row == 0)
      settings.language = (settings.language + direction + 2) % 2;
    else if (row == 1)
      settings.startupPage = (settings.startupPage + direction + 2) % 2;
    else if (row == 2)
      settings.rememberSelection = !settings.rememberSelection;
    else if (row == 3)
      settings.rewind = !settings.rewind;
    else
      settings.runahead = (settings.runahead + direction + 4) % 4;
  } else if (category == 1) {
    if (row == 0)
      settings.fullscreen = !settings.fullscreen;
    else if (row == 1)
      settings.videoAspect = (settings.videoAspect + direction + 3) % 3;
    else if (row == 2)
      settings.videoIntegerScale = !settings.videoIntegerScale;
    else
      // Three stops that wrap, so a press at either end moves the setting
      // instead of doing nothing.
      settings.videoOverscan =
          (settings.videoOverscan + direction * 8 + 24) % 24;
  } else if (category == 2) {
    if (row == 0)
      settings.volume = (settings.volume + direction * 10 + 110) % 110;
    else if (row == 1)
      settings.audioMuted = !settings.audioMuted;
    else
      settings.fastForwardAudio = (settings.fastForwardAudio + 1) % 2;
  } else if (category == 6) {
    if (row == 0)
      settings.accent = (settings.accent + direction + kAccentCount) %
                        kAccentCount;
  } else if (category == 3) {
    if (row == 0) {
      const int value =
          (static_cast<int>(settings.promptDevice) + direction + 3) % 3;
      settings.promptDevice = static_cast<config::PromptDeviceMode>(value);
    } else if (row == 1) {
      const int value =
          (static_cast<int>(settings.controllerStyle) + direction + 5) % 5;
      settings.controllerStyle = static_cast<config::ControllerStyle>(value);
    } else if (row == 3) {
      settings.bindingsPlayer = (settings.bindingsPlayer + direction + 2) % 2;
    } else if (row == 4 + static_cast<int>(config::kInputActionCount)) {
      settings.input.analogDeadZone =
          (settings.input.analogDeadZone - 4000 + direction * 1000 + 27000) %
              27000 +
          4000;
    }
  }
}

void drawSettings(SDL_Renderer *renderer, const AppSettings &settings,
                  int category, int row, float time,
                  bool bindingCapture, std::string_view activeControllerName,
                  config::ControllerStyle resolvedControllerStyle,
                  const std::vector<CoreRow> &cores,
                  const config::InputPrompts &prompts, bool railFocused,
                  float focusOpacity) {
  // The background no longer borrows a game's palette: one accent for every
  // screen, so the picture never changes colour when the library does.
  Game game{};
  game.top = {31, 42, 68, 255};
  game.bottom = {9, 17, 36, 255};
  game.accent = accentColor(settings.accent);
  // Only one control carries the ring: the rail while the page is being
  // chosen, a setting row once the keys have moved into the page.
  const bool contentFocused = !railFocused;
  const Language language = i18n::languageFromSetting(settings.language);
  drawBackground(renderer, game, time);
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {2, 8, 20, 128});

  rounded(renderer, 72, 57, 4, 32, 2, game.accent);
  text(renderer, 92, 54, "MRG PLAYER", 3, {255, 255, 255, 255});
  text(renderer, 92, 99, "RETRO GAMES. TIMELESS PLAY.", 1,
       {167, 184, 207, 255});
  text(renderer, 72, 174, tr(TextId::System, language), 2, game.accent);
  text(renderer, 72, 211, tr(TextId::SettingsTitle, language), 3,
       {255, 255, 255, 255});
  text(renderer, 72, 260, tr(TextId::SettingsSubtitle, language), 1,
       {173, 190, 207, 255});

  rounded(renderer, 72, 334, 330, 470, 22, {5, 14, 31, 218});
  text(renderer, 103, 357, tr(TextId::Preferences, language), 1,
       {139, 160, 178, 255});
  for (int i = 0; i < kCategoryCount; ++i) {
    const int y = 389 + i * 52;
    if (i == category) {
      // The tint always marks the page on screen; the ring only appears when
      // the keys are actually on the rail.
      rounded(renderer, 91, static_cast<float>(y), 292, 48, 14,
              {game.accent.r, game.accent.g, game.accent.b,
               accentWashAlpha(game.accent)});
      if (railFocused)
        drawFocusRing(renderer, 91, static_cast<float>(y), 292, 48, 14,
                      focusOpacity);
    }
    text(renderer, 116, y + 10, tr(categoryText(i), language), 2,
         i == category ? SDL_Color{255, 255, 255, 255}
                       : SDL_Color{165, 183, 199, 255});
  }

  text(renderer, 466, 337, tr(categoryText(category), language), 3,
       {255, 255, 255, 255});
  text(renderer, 466, 376, tr(categoryDescription(category), language), 1,
       {144, 165, 184, 255});

  if (category == 0) {
    const std::string runahead =
        settings.runahead == 0
            ? tr(TextId::Off, language)
            : (settings.runahead == 1
                   ? tr(TextId::FrameOne, language)
                   : std::to_string(settings.runahead) + " " +
                         tr(TextId::Frames, language));
    const std::array<std::pair<TextId, std::string>, kGeneralRowCount> rows{{
        {TextId::Language,
         tr(settings.language == 0 ? TextId::English
                                   : TextId::SimplifiedChinese,
            language)},
        {TextId::StartupPage,
         tr(settings.startupPage == 0 ? TextId::ContinuePlaying
                                      : TextId::GameLibrary,
            language)},
        {TextId::RememberSelection,
         tr(settings.rememberSelection ? TextId::On : TextId::Off, language)},
        {TextId::Rewind,
         tr(settings.rewind ? TextId::On : TextId::Off, language)},
        {TextId::Runahead, runahead},
    }};
    constexpr std::array<TextId, kGeneralRowCount> descriptions{
        TextId::LanguageDescription, TextId::StartupDescription,
        TextId::RememberDescription, TextId::RewindDescription,
        TextId::RunaheadDescription};
    for (int index = 0; index < kGeneralRowCount; ++index) {
      drawSettingRow(renderer, kGeneralRowY + index * kGeneralRowStep,
                     row == index, contentFocused && row == index,
                     tr(rows[index].first, language),
                     tr(descriptions[index], language), rows[index].second,
                     game.accent, kGeneralRowHeight, focusOpacity);
    }
  } else if (category == 1) {
    drawSettingRow(
        renderer, 410, row == 0, contentFocused && row == 0,
        tr(TextId::DisplayMode, language), std::string(),
        tr(settings.fullscreen ? TextId::Fullscreen : TextId::Windowed,
           language),
        game.accent, 92, focusOpacity);
    drawSettingRow(renderer, 516, row == 1, contentFocused && row == 1,
                   tr(TextId::AspectRatio, language),
                   tr(TextId::AspectRatioDescription, language),
                   tr(aspectText(settings.videoAspect), language), game.accent,
                   92, focusOpacity);
    drawSettingRow(
        renderer, 622, row == 2, contentFocused && row == 2,
        tr(TextId::IntegerScale, language),
        tr(TextId::IntegerScaleDescription, language),
        tr(settings.videoIntegerScale ? TextId::On : TextId::Off, language),
        game.accent, 92, focusOpacity);
    drawSettingRow(renderer, 728, row == 3, contentFocused && row == 3,
                   tr(TextId::Overscan, language),
                   tr(TextId::OverscanDescription, language),
                   std::to_string(settings.videoOverscan) + " px",
                   game.accent, 92, focusOpacity);
  } else if (category == 2) {
    drawSettingRow(renderer, 410, row == 0, contentFocused && row == 0,
                   tr(TextId::MasterVolume, language),
                   tr(TextId::VolumeDescription, language),
                   std::to_string(settings.volume) + "%", game.accent, 92,
                   focusOpacity);
    drawSettingRow(renderer, 516, row == 1, contentFocused && row == 1,
                   tr(TextId::Mute, language),
                   tr(TextId::MuteDescription, language),
                   tr(settings.audioMuted ? TextId::On : TextId::Off, language),
                   game.accent, 92, focusOpacity);
    drawSettingRow(
        renderer, 622, row == 2, contentFocused && row == 2,
        tr(TextId::FastForwardAudio, language), std::string(),
        tr(settings.fastForwardAudio != 0 ? TextId::FastForwardAudioKeep
                                          : TextId::FastForwardAudioSilent,
           language),
        game.accent, 92, focusOpacity);
  } else if (category == 3) {
    const std::size_t player =
        static_cast<std::size_t>(std::clamp(settings.bindingsPlayer, 0, 1));
    const auto &keys = config::keyboardBindings(settings.input, player);
    const auto &pads = config::controllerBindings(settings.input, player);
    drawCompactRow(
        renderer, kTrackX, kCompactStartY, kTrackWidth,
        row == 0, contentFocused && row == 0,
        tr(TextId::PromptDevice, language),
        tr(promptDeviceText(settings.promptDevice), language), game.accent,
        focusOpacity);
    drawCompactRow(renderer, kTrackX, kCompactStartY + kCompactStep,
                   kTrackWidth, row == 1, contentFocused && row == 1,
                   tr(TextId::ControllerStyle, language),
                   tr(controllerStyleText(settings.controllerStyle), language),
                   game.accent, focusOpacity);
    drawCompactRow(
        renderer, kTrackX, kCompactStartY + 2 * kCompactStep, kTrackWidth,
        row == 2, contentFocused && row == 2,
        tr(TextId::ActiveController, language),
        activeControllerName.empty()
            ? tr(TextId::NoController, language)
            : std::string(activeControllerName),
        game.accent, focusOpacity);
    drawCompactRow(
        renderer, kTrackX, kCompactStartY + 3 * kCompactStep, kTrackWidth,
        row == 3, contentFocused && row == 3,
        tr(TextId::BindingsPlayer, language),
        tr(player == 0 ? TextId::PlayerOne : TextId::PlayerTwo, language),
        game.accent, focusOpacity);
    for (std::size_t index = 0; index < config::kInputActionCount; ++index) {
      const int bindingRow = static_cast<int>(index) + 4;
      const std::size_t column = index / kBindingsPerColumn;
      const std::size_t line = index % kBindingsPerColumn;
      const int x = column == 0 ? kTrackX : kRightColumnX;
      const int y = kCompactStartY +
                    (4 + static_cast<int>(line)) * kCompactStep;
      const std::string value =
          bindingCapture && row == bindingRow
              ? tr(TextId::PressInput, language)
              : config::keyBindingName(keys[index]) + " / " +
                    config::controllerButtonPrompt(pads[index],
                                                   resolvedControllerStyle);
      drawCompactRow(
          renderer, x, y, kColumnWidth,
          row == bindingRow, contentFocused && row == bindingRow,
          config::inputActionName(static_cast<config::InputAction>(index)),
          value, game.accent, focusOpacity);
    }
    const int deadZoneRow = 4 + static_cast<int>(config::kInputActionCount);
    drawCompactRow(
        renderer, kTrackX,
        kCompactStartY + (4 + static_cast<int>(kBindingsPerColumn)) *
                             kCompactStep,
        kTrackWidth, row == deadZoneRow, contentFocused && row == deadZoneRow,
        tr(TextId::AnalogDeadZone, language),
        std::to_string(settings.input.analogDeadZone), game.accent,
        focusOpacity);
    drawHintRow(renderer, 1528.0f, 784.0f,
                {{tr(TextId::ResetBindingsHint, language), "R", false}},
                game.accent);
  } else if (category == 5) {
    // The refresh button shares the heading line with the page title, so the
    // table below starts on a clean row. A hairline rim keeps it readable as a
    // button on the dark page.
    rounded(renderer, static_cast<float>(kCoreRefreshX - 1),
            static_cast<float>(kCoreRefreshY - 1),
            static_cast<float>(kCoreRefreshWidth + 2),
            static_cast<float>(kCoreRefreshHeight + 2), 12.0f,
            SDL_Color{58, 84, 112, 90});
    rounded(renderer, static_cast<float>(kCoreRefreshX),
            static_cast<float>(kCoreRefreshY),
            static_cast<float>(kCoreRefreshWidth),
            static_cast<float>(kCoreRefreshHeight), 11.0f,
            SDL_Color{26, 42, 64, 235});
    const std::string refresh = tr(TextId::RefreshCores, language);
    text(renderer,
         kCoreRefreshX + (kCoreRefreshWidth - textWidth(refresh, 1)) / 2,
         kCoreRefreshY + 12, refresh, 1, {232, 238, 246, 255});

    // Column captions: the status column is right aligned to the same edge the
    // pills below it use.
    text(renderer, kTrackX + 28, kCoreRowY - 22,
         tr(TextId::ColumnPlatform, language), 1, {120, 140, 162, 255});
    text(renderer, kCoreColumnCoreX, kCoreRowY - 22,
         tr(TextId::ColumnCore, language), 1, {120, 140, 162, 255});
    const std::string statusCaption = tr(TextId::ColumnStatus, language);
    text(renderer, kCorePillRight - textWidth(statusCaption, 1), kCoreRowY - 22,
         statusCaption, 1, {120, 140, 162, 255});

    for (int index = 0; index < kInstallerRowCount; ++index) {
      const int y = kCoreRowY + index * kCoreRowStep;
      const bool hasRow = index < static_cast<int>(cores.size());
      const bool selected = row == index;
      rounded(renderer, static_cast<float>(kTrackX), static_cast<float>(y),
              static_cast<float>(kTrackWidth),
              static_cast<float>(kCoreRowHeight), 12.0f,
              SDL_Color{10, 20, 38, 210});
      if (selected)
        rounded(renderer, static_cast<float>(kTrackX), static_cast<float>(y),
                static_cast<float>(kTrackWidth),
                static_cast<float>(kCoreRowHeight), 12.0f,
                SDL_Color{game.accent.r, game.accent.g, game.accent.b, 90});
      if (selected && contentFocused)
        drawFocusRing(renderer, static_cast<float>(kTrackX),
                      static_cast<float>(y), static_cast<float>(kTrackWidth),
                      static_cast<float>(kCoreRowHeight), 12.0f,
                      focusOpacity);
      if (!hasRow)
        continue;
      const CoreRow &entry = cores[static_cast<std::size_t>(index)];
      text(renderer, kTrackX + 28, y + 15, entry.platform, 2,
           selected ? SDL_Color{255, 255, 255, 255}
                    : SDL_Color{232, 238, 246, 255});
      text(renderer, kCoreColumnCoreX, y + 19, entry.core, 1,
           {176, 193, 211, 255});
      const int pillWidth = textWidth(entry.status, 1) + 26;
      rounded(renderer, static_cast<float>(kCorePillRight - pillWidth),
              static_cast<float>(y + 9), static_cast<float>(pillWidth), 30.0f,
              15.0f,
              entry.installed
                  ? SDL_Color{game.accent.r, game.accent.g, game.accent.b, 70}
                  : SDL_Color{26, 40, 60, 180});
      text(renderer, kCorePillRight - pillWidth + 13, y + 17, entry.status, 1,
           entry.installed ? game.accent
                           : SDL_Color{150, 168, 188, 255});
    }
  } else if (category == 6) {
    const std::string accentName = tr(accentText(settings.accent), language);
    drawSettingRow(renderer, 410, row == 0, contentFocused && row == 0,
                   tr(TextId::AccentColour, language),
                   tr(TextId::AccentDescription, language), accentName,
                   game.accent, 92, focusOpacity);
    // A swatch so the choice reads as a colour and not only a name.
    circle(renderer, 1420 - textWidth(accentName, 2) - 30, 456, 10,
           accentColor(settings.accent));
  } else if (category == 4) {
    drawSettingRow(renderer, 410, true, contentFocused,
                   tr(TextId::RomDirectory, language),
                   tr(TextId::RomDirectoryDescription, language),
                   tr(TextId::ManagedDirectory, language), game.accent, 92,
                   focusOpacity);
  } else {
    drawSettingRow(renderer, 410, true, contentFocused, "MRG PLAYER",
                   tr(TextId::LauncherDescription, language), MRG_VERSION,
                   game.accent, 92, focusOpacity);
  }

  // One legend, anchored bottom right: only the two keys that act here. The
  // direction keys are never written down, the way a console never labels them.
  // Back sits on the left like the cancel button of every dialog, so the
  // action that acts stays on the right.
  // The core page spends its action key on the selected platform instead of
  // editing a value, so the legend is built per page.
  std::vector<Hint> footer{{tr(TextId::Back, language), prompts.back, false}};
  if (category == 5) {
    const bool installedRow =
        row >= 0 && row < static_cast<int>(cores.size()) &&
        cores[static_cast<std::size_t>(row)].installed;
    footer.push_back({tr(installedRow ? TextId::CoreDetails : TextId::InstallCore,
                         language),
                      prompts.confirm, true});
  } else {
    footer.push_back(
        {tr(TextId::ChangeControl, language), prompts.change, true});
  }
  drawHintRow(renderer, 1528.0f, 836.0f, footer, game.accent);
}

} // namespace launcher::ui
