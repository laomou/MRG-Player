#include "ui/SettingsScreen.hpp"

#include "config/InputBindings.hpp"
#include "i18n/Localization.hpp"
#include "model/Game.hpp"
#include "ui/Primitives.hpp"

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

constexpr int kCategoryCount = 7;

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
constexpr std::size_t kBindingsPerColumn = 5;
static_assert(config::kInputActionCount == 2 * kBindingsPerColumn);

// The general page carries five rows now, so its rows are slightly shorter
// than the ones on the other pages.
constexpr int kGeneralRowCount = 5;
constexpr int kGeneralRowY = 404;
constexpr int kGeneralRowStep = 84;
constexpr int kGeneralRowHeight = 76;

TextId categoryText(int category) {
  constexpr std::array<TextId, kCategoryCount> ids{
      TextId::General,    TextId::Display,     TextId::Audio,
      TextId::Controller, TextId::GameLibrary, TextId::Cores,
      TextId::About};
  return ids[static_cast<std::size_t>(
      std::clamp(category, 0, kCategoryCount - 1))];
}

TextId categoryDescription(int category) {
  constexpr std::array<TextId, kCategoryCount> ids{
      TextId::GeneralDescription, TextId::DisplayDescription,
      TextId::AudioDescription,   TextId::ControllerDescription,
      TextId::LibraryDescription, TextId::CoresDescription,
      TextId::AboutDescription};
  return ids[static_cast<std::size_t>(
      std::clamp(category, 0, kCategoryCount - 1))];
}

void drawSettingRow(SDL_Renderer *renderer, int y, bool selected,
                    const std::string &title, const std::string &description,
                    const std::string &value, SDL_Color accent,
                    int height = 92) {
  rounded(renderer, 466, static_cast<float>(y), 1010,
          static_cast<float>(height), 17,
          selected ? SDL_Color{22, 35, 57, 246} : SDL_Color{7, 17, 34, 188});
  if (selected) {
    rounded(renderer, 466, static_cast<float>(y), 6,
            static_cast<float>(height), 3, accent);
    rounded(renderer, 1228, static_cast<float>(y + (height - 50) / 2), 216, 50,
            25, {accent.r, accent.g, accent.b, 54});
  }
  const int titleY = y + (height - 58) / 2;
  text(renderer, 498, titleY, title, 2, {247, 248, 252, 255});
  text(renderer, 498, titleY + 34, description, 1, {148, 169, 187, 255});
  const int valueX = 1420 - textWidth(value, 2);
  text(renderer, valueX, y + (height - 24) / 2, value, 2,
       selected ? accent : SDL_Color{205, 215, 226, 255});
}

// One dense line of the controller page. It takes a rectangle so the binding
// list can be split into two columns without a second implementation.
void drawCompactRow(SDL_Renderer *renderer, int x, int y, int width,
                    bool selected, const std::string &title,
                    const std::string &value, SDL_Color accent) {
  rounded(renderer, static_cast<float>(x), static_cast<float>(y),
          static_cast<float>(width), 29, 9,
          selected ? SDL_Color{22, 35, 57, 246} : SDL_Color{7, 17, 34, 170});
  if (selected)
    rounded(renderer, static_cast<float>(x), static_cast<float>(y), 5, 29, 2,
            accent);
  text(renderer, x + 26, y + 7, title, 1,
       selected ? SDL_Color{255, 255, 255, 255}
                : SDL_Color{184, 198, 216, 255});
  const int valueX = x + width - 20 - textWidth(value, 1);
  text(renderer, valueX, y + 7, value, 1,
       selected ? accent : SDL_Color{205, 215, 226, 255});
}

TextId aspectText(int aspect) {
  if (aspect == 1)
    return TextId::AspectScreen4x3;
  if (aspect == 2)
    return TextId::AspectPixels8x7;
  return TextId::AspectPixel;
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
      settings.videoOverscan =
          std::clamp(settings.videoOverscan + direction * 8, 0, 16);
  } else if (category == 2) {
    if (row == 0)
      settings.volume = std::clamp(settings.volume + direction * 10, 0, 100);
    else if (row == 1)
      settings.audioMuted = !settings.audioMuted;
    else
      settings.fastForwardAudio = (settings.fastForwardAudio + 1) % 2;
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
      settings.input.analogDeadZone = std::clamp(
          settings.input.analogDeadZone + direction * 1000, 4000, 30000);
    }
  }
}

void drawSettings(SDL_Renderer *renderer, const AppSettings &settings,
                  int category, int row, int selectedGame, float time,
                  bool bindingCapture, std::string_view activeControllerName,
                  config::ControllerStyle resolvedControllerStyle,
                  const std::vector<CoreRow> &cores,
                  const config::InputPrompts &prompts) {
  const Game &game = games()[static_cast<std::size_t>(selectedGame)];
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

  rounded(renderer, 72, 334, 330, 424, 22, {5, 14, 31, 218});
  text(renderer, 103, 357, tr(TextId::Preferences, language), 1,
       {139, 160, 178, 255});
  for (int i = 0; i < kCategoryCount; ++i) {
    const int y = 389 + i * 52;
    if (i == category) {
      rounded(renderer, 91, static_cast<float>(y), 292, 48, 14,
              {game.accent.r, game.accent.g, game.accent.b, 54});
      rounded(renderer, 91, static_cast<float>(y), 5, 48, 2, game.accent);
    }
    text(renderer, 116, y + 14, tr(categoryText(i), language), 2,
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
                     row == index, tr(rows[index].first, language),
                     tr(descriptions[index], language), rows[index].second,
                     game.accent, kGeneralRowHeight);
    }
  } else if (category == 1) {
    drawSettingRow(
        renderer, 410, row == 0, tr(TextId::DisplayMode, language),
        tr(TextId::DisplayModeDescription, language),
        tr(settings.fullscreen ? TextId::Fullscreen : TextId::Windowed,
           language),
        game.accent);
    drawSettingRow(renderer, 516, row == 1, tr(TextId::AspectRatio, language),
                   tr(TextId::AspectRatioDescription, language),
                   tr(aspectText(settings.videoAspect), language), game.accent);
    drawSettingRow(
        renderer, 622, row == 2, tr(TextId::IntegerScale, language),
        tr(TextId::IntegerScaleDescription, language),
        tr(settings.videoIntegerScale ? TextId::On : TextId::Off, language),
        game.accent);
    drawSettingRow(renderer, 728, row == 3, tr(TextId::Overscan, language),
                   tr(TextId::OverscanDescription, language),
                   std::to_string(settings.videoOverscan) + " px",
                   game.accent);
  } else if (category == 2) {
    drawSettingRow(renderer, 410, row == 0, tr(TextId::MasterVolume, language),
                   tr(TextId::VolumeDescription, language),
                   std::to_string(settings.volume) + "%", game.accent);
    drawSettingRow(renderer, 516, row == 1, tr(TextId::Mute, language),
                   tr(TextId::MuteDescription, language),
                   tr(settings.audioMuted ? TextId::On : TextId::Off, language),
                   game.accent);
    drawSettingRow(
        renderer, 622, row == 2, tr(TextId::FastForwardAudio, language),
        tr(TextId::FastForwardAudioDescription, language),
        tr(settings.fastForwardAudio != 0 ? TextId::FastForwardAudioKeep
                                          : TextId::FastForwardAudioSilent,
           language),
        game.accent);
  } else if (category == 3) {
    const std::size_t player =
        static_cast<std::size_t>(std::clamp(settings.bindingsPlayer, 0, 1));
    const auto &keys = config::keyboardBindings(settings.input, player);
    const auto &pads = config::controllerBindings(settings.input, player);
    drawCompactRow(
        renderer, kTrackX, kCompactStartY, kTrackWidth, row == 0,
        tr(TextId::PromptDevice, language),
        tr(promptDeviceText(settings.promptDevice), language), game.accent);
    drawCompactRow(renderer, kTrackX, kCompactStartY + kCompactStep,
                   kTrackWidth, row == 1,
                   tr(TextId::ControllerStyle, language),
                   tr(controllerStyleText(settings.controllerStyle), language),
                   game.accent);
    drawCompactRow(
        renderer, kTrackX, kCompactStartY + 2 * kCompactStep, kTrackWidth,
        row == 2, tr(TextId::ActiveController, language),
        activeControllerName.empty()
            ? tr(TextId::NoController, language)
            : std::string(activeControllerName),
        game.accent);
    drawCompactRow(
        renderer, kTrackX, kCompactStartY + 3 * kCompactStep, kTrackWidth,
        row == 3, tr(TextId::BindingsPlayer, language),
        tr(player == 0 ? TextId::PlayerOne : TextId::PlayerTwo, language),
        game.accent);
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
          renderer, x, y, kColumnWidth, row == bindingRow,
          config::inputActionName(static_cast<config::InputAction>(index)),
          value, game.accent);
    }
    const int deadZoneRow = 4 + static_cast<int>(config::kInputActionCount);
    drawCompactRow(
        renderer, kTrackX,
        kCompactStartY + (4 + static_cast<int>(kBindingsPerColumn)) *
                             kCompactStep,
        kTrackWidth, row == deadZoneRow,
        tr(TextId::AnalogDeadZone, language),
        std::to_string(settings.input.analogDeadZone), game.accent);
    drawHintRow(renderer, 1528.0f, 784.0f,
                {{tr(TextId::ResetBindingsHint, language), "R", false}},
                game.accent);
  } else if (category == 5) {
    for (std::size_t index = 0; index < cores.size() && index < 5; ++index) {
      const CoreRow &entry = cores[index];
      const std::string value =
          entry.size.empty()
              ? entry.core + "  " + entry.status
              : entry.core + "  " + entry.size + "  " + entry.status;
      drawCompactRow(renderer, kTrackX, kCompactStartY + 8 +
                                         static_cast<int>(index) * 56,
                     kTrackWidth, row == static_cast<int>(index),
                     entry.platform, value, game.accent);
    }
    text(renderer, kTrackX, 700, tr(TextId::CoreSnapshotNote, language), 1,
         {144, 165, 184, 255});
    drawHintRow(renderer, 1528.0f, 784.0f,
                {{tr(TextId::InstallCore, language), prompts.confirm, false},
                 {tr(TextId::RemoveCore, language), "DEL", false},
                 {tr(TextId::RefreshCores, language), "R", false}},
                game.accent);
  } else if (category == 4) {
    drawSettingRow(renderer, 410, true, tr(TextId::RomDirectory, language),
                   tr(TextId::RomDirectoryDescription, language),
                   tr(TextId::ManagedDirectory, language), game.accent);
  } else {
    drawSettingRow(renderer, 410, true, "MRG PLAYER",
                   tr(TextId::LauncherDescription, language), MRG_VERSION,
                   game.accent);
  }

  // One legend, anchored bottom right: the action first, the control second.
  const std::vector<Hint> footer{
      {tr(TextId::CategoryControl, language), prompts.category, false},
      {tr(TextId::ChangeControl, language), prompts.change, true},
      {tr(TextId::Back, language), prompts.back, false}};
  drawHintRow(renderer, 1528.0f, 836.0f, footer, game.accent);
}

} // namespace launcher::ui
