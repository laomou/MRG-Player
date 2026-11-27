#include "ui/SettingsScreen.hpp"

#include "config/InputBindings.hpp"
#include "i18n/Localization.hpp"
#include "model/Game.hpp"
#include "ui/Primitives.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
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

// The content column starts on the same line as the home shelf: stepping into
// the settings screen then keeps the page rhythm instead of dropping the list
// lower and leaving the last row sitting on top of the footer.
constexpr int kContentTop = 237;
constexpr int kRailTop = kContentTop;
constexpr int kRailHeight = 376;
constexpr int kRailLabelY = kContentTop + 18;
constexpr int kRailItemY = kContentTop + 44;
constexpr int kContentTitleY = kContentTop + 2;
constexpr int kContentDescriptionY = kContentTop + 34;

// Geometry of the dense controller page, shared by drawing and hit testing.
constexpr int kCompactStartY = kContentTop + 61;
constexpr int kCompactStep = 27;
constexpr int kRowHeight = 23;
// The page keeps its left column and its right margin; only the width between
// them changes, so the 1280x720 desktop keeps the exact pixels it had and a
// narrower canvas narrows the rows instead of pushing them off screen.
constexpr int kTrackX = 373;
constexpr int kTrackRightInset = 99;
const int kTrackWidth = kCanvasWidth - kTrackX - kTrackRightInset;
constexpr int kFixedRowCount = 4;
constexpr int kColumnGutter = 13;
// Seven rows per column: the pad actions (A/B/X/Y/L/R with select and start)
// plus the two turbo rows, with the dead-zone slider under both columns.
constexpr std::size_t kBindingsPerColumn = 7;
static_assert(config::kInputActionCount == 2 * kBindingsPerColumn);

// The general page carries five lines -- language, startup page, remembered
// selection, rewind and runahead -- so its rows are slightly shorter than the
// ones on the other pages. Rewind is the one line a pad cannot use: it flips a
// shortcut only a held keyboard key starts, so the page drops it there and the
// line under it moves up into its slot.
constexpr int kGeneralRowCount = 5;
constexpr int kRewindSetting = 3;

constexpr int generalRowCount(bool keyboardPrompts) {
  return keyboardPrompts ? kGeneralRowCount : kGeneralRowCount - 1;
}

// Row on the page -> the general setting it edits, which is the same thing
// except on a page without the rewind line.
constexpr int generalSettingAt(int row, bool keyboardPrompts) {
  return !keyboardPrompts && row >= kRewindSetting ? row + 1 : row;
}
constexpr int kGeneralRowY = kContentTop + 64;
constexpr int kGeneralRowStep = 67;
constexpr int kGeneralRowHeight = 61;

// The single-column pages use 92 pixel rows on a 106 pixel pitch, and the
// installer page lists one platform per 56 pixel line.
constexpr int kSettingRowY = 328;
constexpr int kSettingRowStep = 85;
constexpr int kSettingRowHeight = 74;
constexpr int kInstallerRowCount = 5;

// The core page gives every platform one 48 pixel line on a 56 pixel pitch,
// with the installed counter and the refresh button on the heading line. The
// columns are fixed so the platform name, the core file, and the status never
// wander between rows.
constexpr int kCoreRowY = kContentTop + 80;
constexpr int kCoreRowStep = 45;
constexpr int kCoreRowHeight = 38;
constexpr int kCoreRefreshY = kContentTop - 2;
constexpr int kCoreRefreshWidth = 117;
constexpr int kCoreRefreshHeight = 27;
constexpr int kCoreColumnOffset = 347;
constexpr int kCoreReferenceTrackWidth = 808;
constexpr int kCoreColumnGap = 12;
constexpr int kCorePillInset = 21;
// Preserve the desktop column exactly, but let it move left with the narrower
// track instead of leaving almost no room for filenames on the Brick canvas.
const int kCoreColumnCoreX =
    kTrackX + kTrackWidth * kCoreColumnOffset / kCoreReferenceTrackWidth;
const int kCorePillRight = kTrackX + kTrackWidth - kCorePillInset;
const int kCoreRefreshX = kTrackX + kTrackWidth - kCoreRefreshWidth;
const int kFooterX = kCanvasWidth - 58;
const int kFooterY = kCanvasHeight - 51;

const LinearLayout kGeneralRows{
    {kTrackX, kGeneralRowY, kTrackWidth, kGeneralRowHeight},
    kGeneralRowCount, kGeneralRowStep - kGeneralRowHeight};
const LinearLayout kSettingRows{
    {kTrackX, kSettingRowY, kTrackWidth, kSettingRowHeight},
    4, kSettingRowStep - kSettingRowHeight};
const LinearLayout kCoreRows{{kTrackX, kCoreRowY, kTrackWidth, kCoreRowHeight},
                              kInstallerRowCount, kCoreRowStep - kCoreRowHeight};
const LinearLayout kCompactRows{{kTrackX, kCompactStartY, kTrackWidth, kRowHeight},
                                 kFixedRowCount + kBindingsPerColumn + 1,
                                 kCompactStep - kRowHeight};
const LinearLayout kRailRows{{73, kRailItemY, 234, 38}, kCategoryCount, 3};
const Rect kCoreRefresh{kCoreRefreshX, kCoreRefreshY,
                        kCoreRefreshWidth, kCoreRefreshHeight};

Rect controllerRowBounds(int row) {
  if (row < 0 || row >= kFixedRowCount + static_cast<int>(config::kInputActionCount) + 1)
    return {};
  if (row < kFixedRowCount)
    return kCompactRows.item(row);
  const int binding = row - kFixedRowCount;
  if (binding == static_cast<int>(config::kInputActionCount))
    return kCompactRows.item(kFixedRowCount + kBindingsPerColumn);
  const int line = binding % static_cast<int>(kBindingsPerColumn);
  const int column = binding / static_cast<int>(kBindingsPerColumn);
  return equalColumns(kCompactRows.item(kFixedRowCount + line), 2,
                      kColumnGutter).item(column);
}

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

void drawSettingRow(SDL_Renderer *renderer, Rect bounds, bool selected,
                    bool focused, const std::string &title,
                    const std::string &description, const std::string &value,
                    SDL_Color accent, float focusOpacity = 1.0f) {
  // The wash marks the row the keys are acting on, exactly like the tint on the
  // category rail, and the ring only appears when the keys are actually in the
  // page. The ring goes on last so it stays a line rather than a wash over the
  // plate.
  const int y = static_cast<int>(bounds.y);
  const int height = static_cast<int>(bounds.h);
  drawSelectionPlate(renderer, bounds, 17, {7, 17, 34, 188},
                     {accent.r, accent.g, accent.b, accentWashAlpha(accent)},
                     selected, focused, focusOpacity);
  // A row without a description puts the label itself on the centre line, so the
  // page keeps one rhythm instead of a ragged column of top-aligned labels.
  const bool hasDescription = !description.empty();
  const int titleY = hasDescription ? y + (height - 58) / 2 : y + (height - 34) / 2;
  const auto columns = labelValueLayout(bounds.inset(26, 0, 45, 0),
                                        textWidth(value, 2));
  const std::string fittedValue = fitText(value, columns.value.w, 2);
  text(renderer, columns.label.x, titleY,
       fitText(title, columns.label.w, 2), 2,
       selected || focused ? SDL_Color{255, 255, 255, 255}
                           : SDL_Color{247, 248, 252, 255});
  if (hasDescription)
    text(renderer, columns.label.x, titleY + 34,
         fitText(description, columns.label.w, 1), 1, {148, 169, 187, 255});
  const int valueX = columns.value.right() - textWidth(fittedValue, 2);
  text(renderer, valueX, textCenteredY(fittedValue, y + height / 2, 2),
       fittedValue, 2,
       selected || focused ? accent : SDL_Color{205, 215, 226, 255});

}

// One dense line of the controller page. It takes a rectangle so the binding
// list can be split into two columns without a second implementation.
void drawCompactRow(SDL_Renderer *renderer, Rect bounds,
                    bool selected, bool focused, const std::string &title,
                    const std::string &value, SDL_Color accent,
                    float focusOpacity = 1.0f) {
  drawSelectionPlate(renderer, bounds, 9, {7, 17, 34, 170},
                     {accent.r, accent.g, accent.b, accentWashAlpha(accent)},
                     selected, focused, focusOpacity);
  const int centerY = static_cast<int>(bounds.centerY());
  const auto columns = labelValueLayout(bounds.inset(21, 0, 16, 0),
                                        textWidth(value, 1));
  const std::string fittedTitle = fitText(title, columns.label.w, 1);
  const std::string fittedValue = fitText(value, columns.value.w, 1);
  text(renderer, columns.label.x, textCenteredY(fittedTitle, centerY, 1),
       fittedTitle, 1,
       selected || focused ? SDL_Color{255, 255, 255, 255}
                           : SDL_Color{184, 198, 216, 255});
  const int valueX = columns.value.right() - textWidth(fittedValue, 1);
  text(renderer, valueX, textCenteredY(fittedValue, centerY, 1), fittedValue, 1,
       selected || focused ? accent : SDL_Color{205, 215, 226, 255});

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

CoreRowColumns coreRowColumns(const std::string &status) {
  const int statusWidth = textWidth(status, 1) + 26;
  const int statusX = kCorePillRight - statusWidth;
  const int filenameWidth =
      std::max(0, statusX - kCoreColumnGap - kCoreColumnCoreX);
  return {{static_cast<float>(kCoreColumnCoreX), 0.0f,
           static_cast<float>(filenameWidth), 0.0f},
          {static_cast<float>(statusX), 0.0f,
           static_cast<float>(statusWidth), 0.0f}};
}

#if defined(MRG_PLATFORM_BRICK)
// The case lighting only exists on the handheld, so the theme page grows a
// second row there and stays a single row everywhere else.
constexpr int kThemeRowCount = 2;
#else
constexpr int kThemeRowCount = 1;
#endif

int settingCategoryCount() { return kCategoryCount; }

int settingRowCount(int category, bool keyboardPrompts) {
  if (category == 0)
    return generalRowCount(keyboardPrompts);
  if (category == 1)
    return 4;
  if (category == 2)
    return 3;
  if (category == 3)
    return 4 + static_cast<int>(config::kInputActionCount) + 1;
  if (category == 5)
    return 5;
  if (category == 6)
    return kThemeRowCount;
  return 1;
}

FocusGrid settingsControls(int category, bool keyboardPrompts) {
  ControlList rail;
  std::vector<Control> categories;
  categories.reserve(static_cast<std::size_t>(kCategoryCount));
  for (int index = 0; index < kCategoryCount; ++index)
    categories.push_back(Control{index, true});
  rail.reset(std::move(categories));

  if (category == 3) {
    // The controller page is the one page whose rows are cut into two lanes:
    // four full width rows, then the bindings side by side, then the dead zone.
    // Sharing a line number is what lets left and right cross between the two
    // halves of a binding instead of handing the keys back to the rail.
    const int actions = static_cast<int>(config::kInputActionCount);
    const int lanes = static_cast<int>(kBindingsPerColumn);
    std::vector<Control> left;
    std::vector<Control> right;
    left.reserve(static_cast<std::size_t>(kFixedRowCount + kBindingsPerColumn) + 1);
    right.reserve(kBindingsPerColumn);
    for (int index = 0; index < kFixedRowCount; ++index)
      left.push_back(Control{index, true, index});
    for (int index = 0; index < lanes; ++index) {
      const int line = kFixedRowCount + index;
      const int id = kFixedRowCount + index;
      left.push_back(Control{id, true, line});
      right.push_back(Control{id + lanes, true, line});
    }
    left.push_back(Control{kFixedRowCount + actions, true,
                           kFixedRowCount + lanes});
    ControlList leftLane;
    leftLane.reset(std::move(left), false);
    ControlList rightLane;
    rightLane.reset(std::move(right), false);
    FocusGrid grid;
    grid.reset({std::move(rail), std::move(leftLane), std::move(rightLane)});
    return grid;
  }

  std::vector<Control> rows;
  const int rowCount = settingRowCount(category, keyboardPrompts);
  rows.reserve(static_cast<std::size_t>(rowCount) + 1);
  // The refresh button sits above the first platform, so it is the first entry
  // of the column rather than a row index with a special meaning.
  if (category == 5)
    rows.push_back(Control{kSettingsRefreshControlId, true, 0});
  for (int index = 0; index < rowCount; ++index)
    rows.push_back(Control{index, true, index});
  ControlList content;
  content.reset(std::move(rows), false);

  FocusGrid grid;
  grid.reset({std::move(rail), std::move(content)});
  return grid;
}

int controllerPageRowAt(int x, int y) {
  for (int row = 0; row < kFixedRowCount + static_cast<int>(config::kInputActionCount) + 1; ++row)
    if (controllerRowBounds(row).contains(x, y))
      return row;
  return -1;
}

int generalPageRowAt(float x, float y, bool keyboardPrompts) {
  LinearLayout rows = kGeneralRows;
  rows.count = generalRowCount(keyboardPrompts);
  return rows.hit(x, y);
}

int settingsCategoryAt(float x, float y) {
  for (int category = 0; category < kCategoryCount; ++category)
    if (kRailRows.item(category).expanded(15, 0).contains(x, y))
      return category;
  return -1;
}

int settingsRowAt(int category, float x, float y, bool keyboardPrompts) {
  if (category < 0 || category >= kCategoryCount)
    return -1;
  if (category == 0)
    return generalPageRowAt(x, y, keyboardPrompts);
  if (category == 3) {
    for (int row = 0; row < kFixedRowCount + static_cast<int>(config::kInputActionCount) + 1; ++row)
      if (controllerRowBounds(row).contains(x, y))
        return row;
    return -1;
  }
  if (category == 5)
    return kCoreRows.hit(x, y);
  auto rows = kSettingRows;
  rows.count = category == 1 ? 4
               : (category == 2 ? 3 : (category == 6 ? kThemeRowCount : 1));
  return rows.hit(x, y);
}

bool coreRefreshAt(float x, float y) {
  return kCoreRefresh.contains(x, y);
}

void changeSetting(AppSettings &settings, int category, int row,
                   int direction, bool keyboardPrompts) {
  if (category == 0) {
    switch (generalSettingAt(row, keyboardPrompts)) {
    case 0:
      settings.language = (settings.language + direction + 2) % 2;
      break;
    case 1:
      settings.startupPage = (settings.startupPage + direction + 2) % 2;
      break;
    case 2:
      settings.rememberSelection = !settings.rememberSelection;
      break;
    case kRewindSetting:
      settings.rewind = !settings.rewind;
      break;
    default:
      settings.runahead = (settings.runahead + direction + 4) % 4;
      break;
    }
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
    else if (kThemeRowCount > 1)
      settings.ledLighting = !settings.ledLighting;
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
                  int category, int row, bool bindingCapture, std::string_view activeControllerName,
                  config::ControllerStyle resolvedControllerStyle,
                  const std::vector<CoreRow> &cores,
                  const config::InputPrompts &prompts, bool railFocused,
                  float focusOpacity, bool refreshFocused, bool footerFocused,
                  int footerAction) {
  // The background no longer borrows a game's palette: one accent for every
  // screen, so the picture never changes colour when the library does.
  Game game{};
  game.top = {31, 42, 68, 255};
  game.bottom = {9, 17, 36, 255};
  game.accent = accentColor(settings.accent);
  // Only one control carries the ring: the rail while the page is being
  // chosen, a row once the keys have moved into the page, the footer's button
  // once they have left it. The core page's refresh button is a control of its
  // own, so while it owns the keys no row draws a ring.
  const bool contentFocused = !railFocused && !refreshFocused && !footerFocused;
  const bool refreshRing = !railFocused && refreshFocused && !footerFocused;
  const bool railRing = railFocused && !footerFocused;
  const Language language = i18n::languageFromSetting(settings.language);
  drawBackground(renderer, game);
  fill(renderer, 0, 0, kCanvasWidth, kCanvasHeight, {2, 8, 20, 128});

  rounded(renderer, 58, 46, 4, 26, 2, game.accent);
  text(renderer, 74, 43, "MRG PLAYER", 3, {255, 255, 255, 255});
  text(renderer, 74, 79, "RETRO GAMES. TIMELESS PLAY.", 1,
       {167, 184, 207, 255});
  // Same header rhythm as the library: the section label, the page title and
  // the subtitle sit on the lines the home screen uses, so the content below
  // starts on the same shelf line on both pages. The block is pulled up so the
  // air above it (under the wordmark) matches the air below it.
  text(renderer, 58, 128, tr(TextId::System, language), 2, game.accent);
  text(renderer, 58, 154, tr(TextId::SettingsTitle, language), 3,
       {255, 255, 255, 255});
  text(renderer, 58, 190, tr(TextId::SettingsSubtitle, language), 1,
       {173, 190, 207, 255});

  rounded(renderer, 58, static_cast<float>(kRailTop), 264,
          static_cast<float>(kRailHeight), 22, {5, 14, 31, 218});
  text(renderer, 82, kRailLabelY, tr(TextId::Preferences, language), 1,
       {139, 160, 178, 255});
  for (int i = 0; i < kCategoryCount; ++i) {
    const Rect bounds = kRailRows.item(i);
    const int y = static_cast<int>(bounds.y);
    // Selected page and actual keyboard focus remain independent.
    drawSelectionPlate(renderer, bounds, 14, {0, 0, 0, 0},
                       {game.accent.r, game.accent.g, game.accent.b,
                        accentWashAlpha(game.accent)},
                       i == category, i == category && railRing, focusOpacity);
    const std::string label = tr(categoryText(i), language);
    text(renderer, 93, textCenteredY(label, y + 19, 2), label, 2,
         i == category ? SDL_Color{255, 255, 255, 255}
                       : SDL_Color{165, 183, 199, 255});
  }

  text(renderer, kTrackX, kContentTitleY, tr(categoryText(category), language),
       3, {255, 255, 255, 255});
  text(renderer, kTrackX, kContentDescriptionY,
       tr(categoryDescription(category), language), 1, {144, 165, 184, 255});

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
    const std::array<TextId, kGeneralRowCount> descriptions{
        TextId::LanguageDescription, TextId::StartupDescription,
        TextId::RememberDescription, TextId::RewindDescription,
        TextId::RunaheadDescription};
    // A pad has no key to hold, so the rewind line is left out rather than
    // shown as a switch that turns nothing on.
    const bool keyboardPrompts = prompts.source.fromKeyboard();
    const int rowCount = generalRowCount(keyboardPrompts);
    for (int index = 0; index < rowCount; ++index) {
      const int setting = generalSettingAt(index, keyboardPrompts);
      drawSettingRow(renderer, kGeneralRows.item(index),
                     row == index, contentFocused && row == index,
                     tr(rows[setting].first, language),
                     tr(descriptions[setting], language),
                     rows[setting].second, game.accent, focusOpacity);
    }
  } else if (category == 1) {
    drawSettingRow(
        renderer, kSettingRows.item(0), row == 0, contentFocused && row == 0,
        tr(TextId::DisplayMode, language), std::string(),
        tr(settings.fullscreen ? TextId::Fullscreen : TextId::Windowed,
           language),
        game.accent, focusOpacity);
    drawSettingRow(renderer, kSettingRows.item(1), row == 1, contentFocused && row == 1,
                   tr(TextId::AspectRatio, language),
                   tr(TextId::AspectRatioDescription, language),
                   tr(aspectText(settings.videoAspect), language), game.accent,
                   focusOpacity);
    drawSettingRow(
        renderer, kSettingRows.item(2), row == 2, contentFocused && row == 2,
        tr(TextId::IntegerScale, language),
        tr(TextId::IntegerScaleDescription, language),
        tr(settings.videoIntegerScale ? TextId::On : TextId::Off, language),
        game.accent, focusOpacity);
    drawSettingRow(renderer, kSettingRows.item(3), row == 3, contentFocused && row == 3,
                   tr(TextId::Overscan, language),
                   tr(TextId::OverscanDescription, language),
                   std::to_string(settings.videoOverscan) + " px",
                   game.accent, focusOpacity);
  } else if (category == 2) {
    drawSettingRow(renderer, kSettingRows.item(0), row == 0, contentFocused && row == 0,
                   tr(TextId::MasterVolume, language),
                   tr(TextId::VolumeDescription, language),
                   std::to_string(settings.volume) + "%", game.accent,
                   focusOpacity);
    drawSettingRow(renderer, kSettingRows.item(1), row == 1, contentFocused && row == 1,
                   tr(TextId::Mute, language),
                   tr(TextId::MuteDescription, language),
                   tr(settings.audioMuted ? TextId::On : TextId::Off, language),
                   game.accent, focusOpacity);
    drawSettingRow(
        renderer, kSettingRows.item(2), row == 2, contentFocused && row == 2,
        tr(TextId::FastForwardAudio, language), std::string(),
        tr(settings.fastForwardAudio != 0 ? TextId::FastForwardAudioKeep
                                          : TextId::FastForwardAudioSilent,
           language),
        game.accent, focusOpacity);
  } else if (category == 3) {
    const std::size_t player =
        static_cast<std::size_t>(std::clamp(settings.bindingsPlayer, 0, 1));
    const auto &keys = config::keyboardBindings(settings.input, player);
    const auto &pads = config::controllerBindings(settings.input, player);
    drawCompactRow(
        renderer, controllerRowBounds(0),
        row == 0, contentFocused && row == 0,
        tr(TextId::PromptDevice, language),
        tr(promptDeviceText(settings.promptDevice), language), game.accent,
        focusOpacity);
    drawCompactRow(renderer, controllerRowBounds(1), row == 1, contentFocused && row == 1,
                   tr(TextId::ControllerStyle, language),
                   tr(controllerStyleText(settings.controllerStyle), language),
                   game.accent, focusOpacity);
    drawCompactRow(
        renderer, controllerRowBounds(2),
        row == 2, contentFocused && row == 2,
        tr(TextId::ActiveController, language),
        activeControllerName.empty()
            ? tr(TextId::NoController, language)
            : std::string(activeControllerName),
        game.accent, focusOpacity);
    drawCompactRow(
        renderer, controllerRowBounds(3),
        row == 3, contentFocused && row == 3,
        tr(TextId::BindingsPlayer, language),
        tr(player == 0 ? TextId::PlayerOne : TextId::PlayerTwo, language),
        game.accent, focusOpacity);
    for (std::size_t index = 0; index < config::kInputActionCount; ++index) {
      const int bindingRow = static_cast<int>(index) + 4;
      const std::string value =
          bindingCapture && row == bindingRow
              ? tr(TextId::PressInput, language)
              : config::keyBindingName(keys[index]) + " / " +
                    config::controllerButtonPrompt(pads[index],
                                                   resolvedControllerStyle);
      drawCompactRow(
          renderer, controllerRowBounds(bindingRow),
          row == bindingRow, contentFocused && row == bindingRow,
          config::inputActionName(static_cast<config::InputAction>(index)),
          value, game.accent, focusOpacity);
    }
    const int deadZoneRow = 4 + static_cast<int>(config::kInputActionCount);
    drawCompactRow(
        renderer, controllerRowBounds(deadZoneRow), row == deadZoneRow, contentFocused && row == deadZoneRow,
        tr(TextId::AnalogDeadZone, language),
        std::to_string(settings.input.analogDeadZone), game.accent,
        focusOpacity);
    // The reset hint sits on the heading line: with fourteen bindings on the
    // page the list needs the room under it. R is a keyboard key and only the
    // keyboard answers to it, so a pad with no such key gets no hint at all.
    if (prompts.source.fromKeyboard())
      drawHintRow(renderer, static_cast<float>(kTrackX + kTrackWidth),
                  static_cast<float>(kContentDescriptionY),
                  {{tr(TextId::ResetBindingsHint, language), "R", false}},
                  game.accent);
  } else if (category == 5) {
    // The refresh button shares the heading line with the page title, so the
    // table below starts on a clean row. A hairline rim keeps it readable as a
    // button on the dark page.
    drawButton(renderer, kCoreRefresh.x, kCoreRefresh.y, kCoreRefresh.w, kCoreRefresh.h, 11.0f,
               tr(TextId::RefreshCores, language), 1,
               SDL_Color{26, 42, 64, 235}, SDL_Color{58, 84, 112, 90},
               SDL_Color{232, 238, 246, 255}, refreshRing, focusOpacity);

    // Column captions: the status column is right aligned to the same edge the
    // pills below it use.
    text(renderer, kTrackX + 22, kCoreRowY - 18,
         tr(TextId::ColumnPlatform, language), 1, {120, 140, 162, 255});
    text(renderer, kCoreColumnCoreX, kCoreRowY - 18,
         tr(TextId::ColumnCore, language), 1, {120, 140, 162, 255});
    const std::string statusCaption = tr(TextId::ColumnStatus, language);
    text(renderer, kCorePillRight - textWidth(statusCaption, 1), kCoreRowY - 18,
         statusCaption, 1, {120, 140, 162, 255});

    for (int index = 0; index < kInstallerRowCount; ++index) {
      const Rect bounds = kCoreRows.item(index);
      const int y = static_cast<int>(bounds.y);
      const bool hasRow = index < static_cast<int>(cores.size());
      const bool selected = row == index;
      drawSelectionPlate(renderer, bounds, 12, {10, 20, 38, 210},
                         {game.accent.r, game.accent.g, game.accent.b, 90},
                         selected, selected && contentFocused, focusOpacity);
      if (!hasRow)
        continue;
      const CoreRow &entry = cores[static_cast<std::size_t>(index)];
      const int centerY = y + kCoreRowHeight / 2;
      text(renderer, kTrackX + 22, textCenteredY(entry.platform, centerY, 2),
           entry.platform, 2,
           selected ? SDL_Color{255, 255, 255, 255}
                    : SDL_Color{232, 238, 246, 255});
      const CoreRowColumns columns = coreRowColumns(entry.status);
      const std::string fittedCore =
          fitText(entry.core, static_cast<int>(columns.filename.w), 1);
      text(renderer, static_cast<int>(columns.filename.x),
           textCenteredY(fittedCore, centerY, 1), fittedCore, 1,
           {176, 193, 211, 255});
      rounded(renderer, columns.status.x, static_cast<float>(y + 4),
              columns.status.w, 30.0f, 15.0f,
              entry.installed
                  ? SDL_Color{game.accent.r, game.accent.g, game.accent.b, 70}
                  : SDL_Color{26, 40, 60, 180});
      text(renderer, static_cast<int>(columns.status.x) + 13,
           textCenteredY(entry.status, y + 24, 1), entry.status, 1,
           entry.installed ? game.accent
                           : SDL_Color{150, 168, 188, 255});
    }
  } else if (category == 6) {
    const std::string accentName = tr(accentText(settings.accent), language);
    drawSettingRow(renderer, kSettingRows.item(0), row == 0, contentFocused && row == 0,
                   tr(TextId::AccentColour, language),
                   tr(TextId::AccentDescription, language), accentName,
                   game.accent, focusOpacity);
    // A swatch so the choice reads as a colour and not only a name. It sits
    // just left of the value, on the row's centre line.
    const Rect themeRow = kSettingRows.item(0);
    const auto themeColumns = labelValueLayout(themeRow.inset(26, 0, 45, 0),
                                              textWidth(accentName, 2));
    circle(renderer,
           static_cast<int>(themeColumns.value.right() -
                            textWidth(accentName, 2) - 24),
           static_cast<int>(themeRow.centerY()), 10,
           accentColor(settings.accent));
    if (kThemeRowCount > 1)
      drawSettingRow(renderer, kSettingRows.item(1), row == 1,
                     contentFocused && row == 1,
                     tr(TextId::LedLighting, language),
                     tr(TextId::LedDescription, language),
                     tr(settings.ledLighting ? TextId::On : TextId::Off,
                        language),
                     game.accent, focusOpacity);
  } else if (category == 4) {
    drawSettingRow(renderer, kSettingRows.item(0), true, contentFocused,
                   tr(TextId::RomDirectory, language),
                   tr(TextId::RomDirectoryDescription, language),
                   tr(TextId::ManagedDirectory, language), game.accent,
                   focusOpacity);
  } else {
    drawSettingRow(renderer, kSettingRows.item(0), true, contentFocused, "MRG PLAYER",
                   tr(TextId::LauncherDescription, language), MRG_VERSION,
                   game.accent, focusOpacity);
  }

  // One row of buttons, anchored bottom right: back on the left like the cancel
  // button of every dialog, and whatever this page does with the focused row on
  // the right. The core page spends its action key on the selected platform
  // instead of editing a value, so the labels are built per page.
  drawActionRow(renderer, static_cast<float>(kFooterX),
                static_cast<float>(kFooterY),
                settingsFooterHints(category, row, cores, refreshFocused,
                                    language, prompts),
                footerFocused ? footerAction : -1, focusOpacity, game.accent);
}

std::vector<Hint> settingsFooterHints(int category, int row,
                                      const std::vector<CoreRow> &cores,
                                      bool refreshFocused, Language language,
                                      const config::InputPrompts &prompts) {
  std::vector<Hint> footer{
      {i18n::tr(i18n::TextId::Back, language), prompts.back, false}};
  if (category == 5) {
    const bool installedRow =
        row >= 0 && row < static_cast<int>(cores.size()) &&
        cores[static_cast<std::size_t>(row)].installed;
    const i18n::TextId action =
        refreshFocused
            ? i18n::TextId::HintRefresh
            : (installedRow ? i18n::TextId::HintDetails
                            : i18n::TextId::HintInstall);
    footer.push_back({i18n::tr(action, language), prompts.confirm, true});
  } else {
    footer.push_back(
        {i18n::tr(i18n::TextId::ChangeControl, language), prompts.confirm, true});
  }
  return footer;
}

int settingsFooterAt(float x, float y, const std::vector<Hint> &hints) {
  const auto layout = actionLayout(static_cast<float>(kFooterX),
                                   static_cast<float>(kFooterY), hints);
  for (std::size_t index = 0; index < layout.size(); ++index) {
    if (layout[index].expanded(4, 6).contains(x, y))
      return static_cast<int>(index);
  }
  return -1;
}

} // namespace launcher::ui
