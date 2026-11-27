#include "ui/GameManagementScreen.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "game/GamePlatform.hpp"
#include "i18n/Localization.hpp"
#include "ui/Primitives.hpp"
#include "util/Utf8Path.hpp"

namespace launcher::ui {
namespace {

using i18n::TextId;
using i18n::tr;

// The drawer hugs the right edge and fills the canvas height; the dialogs stay
// centred on the canvas, so every rectangle keeps its 1280x720 pixels and only
// the drawer grows on a taller device.
struct ManagementLayout {
  PanelLayout panel;
  LinearLayout actions;
  Rect confirm;
  LinearLayout confirmButtons;
  Rect info;
  Rect infoBack;
};

const ManagementLayout &managementLayout() {
  static const ManagementLayout layout = [] {
    constexpr float kMargin = 57.0f;
    constexpr float kPanelWidth = 437.0f;
    const float width = static_cast<float>(kCanvasWidth);
    const float height = static_cast<float>(kCanvasHeight);
    // The drawer is exactly as tall as its six rows: it floats in the middle of
    // the canvas instead of running the full height with a hole under the last
    // action, where the footer legend used to live.
    constexpr float kTopInset = 29.0f;
    constexpr float kBottomInset = 24.0f;
    constexpr float kHeaderHeight = 103.0f;
    constexpr float kGap = 13.0f;
    constexpr float kRowsTop = 15.0f;
    constexpr float kRowHeight = 54.0f;
    constexpr float kRowGap = 14.0f;
    constexpr float kSectionGap = 14.0f;
    const float rowsHeight = kManagementActionCount * kRowHeight +
                             (kManagementActionCount - 1) * kRowGap +
                             kSectionGap;
    const float panelHeight = kTopInset + kHeaderHeight + 2 * kGap + kRowsTop +
                              rowsHeight + kBottomInset;
    const Rect bounds{width - kMargin - kPanelWidth, (height - panelHeight) / 2,
                      kPanelWidth, panelHeight};
    const PanelLayout panel = panelLayout(
        bounds, bounds.inset(26, kTopInset, 26, kBottomInset), kHeaderHeight, 0,
        kGap);
    ManagementLayout out;
    out.panel = panel;
    out.actions = {{panel.content.x, panel.content.y + kRowsTop,
                    panel.content.w, kRowHeight},
                   kManagementActionCount, kRowGap, Axis::Vertical, 4,
                   kSectionGap};
    out.confirm = {(width - 792) / 2, (height - 480) / 2, 792, 480};
    out.confirmButtons =
        equalColumns({out.confirm.x + 51, out.confirm.y + 356,
                      out.confirm.w - 102, 53}, 2, 30);
    out.info = {(width - 712) / 2, (height - 488) / 2, 712, 488};
    out.infoBack = {out.info.x + 48, out.info.bottom() - 108, out.info.w - 96,
                    54};
    return out;
  }();
  return layout;
}

void drawMenu(SDL_Renderer *renderer, const library::GameEntry &entry,
              int selectedAction, bool saveDataPresent, i18n::Language language,
              SDL_Color themeAccent) {
  const ManagementLayout &layout = managementLayout();
  const PanelLayout &panel = layout.panel;
  const int titleX = static_cast<int>(panel.bounds.x + 25);
  const int headerY = static_cast<int>(panel.header.y);
  drawPanel(renderer, panel.bounds, 28, {12, 21, 37, 232},
            panel.bounds.expanded(8, 0).translated(0, 8), {0, 0, 0, 42});
  text(renderer, titleX, headerY, tr(TextId::GameOptions, language), 3,
       {246, 248, 252, 255});
  text(renderer, titleX, headerY + 39,
       fitText(entry.title, static_cast<int>(panel.header.w), 2), 2,
       themeAccent);
  const auto &platform = game::platformDescriptor(entry.platform);
  text(renderer, titleX, headerY + 70,
       std::string(platform.displayName) + "  /  " +
           tr(saveDataPresent ? TextId::SaveDataPresent
                              : TextId::SaveDataMissing,
              language),
       1, {180, 195, 213, 255});
  fill(renderer, panel.header.x, panel.header.bottom(), panel.header.w, 1,
       {255, 255, 255, 30});

  // Frequent actions first; destructive actions are separated at the bottom.
  const std::array<TextId, kManagementActionCount> labels{
      TextId::Start,
      entry.favorite ? TextId::Unfavorite : TextId::Favorite,
      TextId::GameInfo,
      TextId::Cancel,
      TextId::RemoveFromLibrary,
      TextId::DeleteSaveData};
  // Actions whose label already says everything carry no second line; the row
  // then centres the label instead of leaving a hole underneath it.
  const std::array<std::string, kManagementActionCount> descriptions{
      std::string(),
      tr(TextId::FavoriteDescription, language),
      tr(TextId::GameInfoDescription, language),
      std::string(),
      tr(TextId::RemoveRomDescription, language),
      tr(saveDataPresent ? TextId::DeleteSaveDescription : TextId::NoSaveData,
         language)};
  for (int index = 0; index < kManagementActionCount; ++index) {
    const Rect bounds = layout.actions.item(index);
    const float y = bounds.y;
    const bool selected = index == selectedAction;
    const bool destructive = index >= 4;
    const SDL_Color rowAccent =
        destructive ? SDL_Color{255, 157, 150, 255} : themeAccent;
    drawFocusablePlate(renderer, bounds.x, bounds.y, bounds.w, bounds.h, 12,
                       selected      ? rowAccent
                       : destructive ? SDL_Color{255, 108, 104, 12}
                                     : SDL_Color{255, 255, 255, 12},
                       selected);
    // The row is its title and nothing else: a key glyph in front of it only
    // repeated what the action row at the bottom of the shelf already said, and
    // its fixed width pushed the title sideways.
    text(renderer, static_cast<int>(panel.content.x + 18),
         static_cast<int>(y + (descriptions[index].empty() ? 17 : 3)),
         tr(labels[index], language), 2,
         selected      ? SDL_Color{20, 24, 35, 255}
         : destructive ? rowAccent
                       : SDL_Color{241, 245, 251, 255});
    if (!descriptions[index].empty()) {
      text(renderer, static_cast<int>(panel.content.x + 18),
           static_cast<int>(y + 31),
           fitText(descriptions[index],
                   static_cast<int>(panel.content.w - 36), 1),
           1,
           selected ? SDL_Color{38, 48, 54, 255}
                    : SDL_Color{165, 181, 204, 255});
    }
  }
  fill(renderer, panel.content.x, layout.actions.item(4).y - 18,
       panel.content.w, 1, {255, 157, 150, 36});
  // No key legend: the drawer has a CANCEL row of its own, so a strip under it
  // would only name the same two keys a second time.
}

void drawConfirmation(SDL_Renderer *renderer, const library::GameEntry &entry,
                      ManagementDialog dialog, int confirmationChoice,
                      i18n::Language language, SDL_Color accent) {
  const ManagementLayout &layout = managementLayout();
  const Rect &bounds = layout.confirm;
  const int x = static_cast<int>(bounds.x);
  const int y = static_cast<int>(bounds.y);
  fill(renderer, 0, 0, kCanvasWidth, kCanvasHeight, {2, 5, 12, 205});
  rounded(renderer, bounds.x, bounds.y, bounds.w, bounds.h, 34,
          {8, 16, 31, 255});
  fill(renderer, bounds.x, bounds.y, bounds.w, 9, accent);
  circle(renderer, x + 83, y + 83, 32, {255, 98, 94, 36});
  centeredText(renderer, x + 83, y + 72, "!", 4, {255, 133, 128, 255});
  const bool removeRom = dialog == ManagementDialog::ConfirmRemoveRom;
  text(renderer, x + 138, y + 52,
       tr(removeRom ? TextId::RemoveGame : TextId::DeleteSaveData, language), 2,
       {255, 143, 136, 255});
  text(renderer, x + 138, y + 86,
       tr(removeRom ? TextId::RemoveConfirmTitle
                    : TextId::DeleteSaveConfirmTitle,
          language),
       4, {255, 255, 255, 255});
  text(renderer, x + 51, y + 148,
       tr(removeRom ? TextId::RemoveConfirmDescription
                    : TextId::DeleteSaveConfirmDescription,
          language),
       2, {224, 229, 239, 255});
  rounded(renderer, x + 51, y + 170, bounds.w - 102, 69, 14,
          {255, 255, 255, 14});
  text(renderer, x + 71, y + 186, tr(TextId::File, language), 1,
       {132, 145, 169, 255});
  text(renderer, x + 71, y + 209,
       fitText(paths::toUtf8(paths::fromUtf8(entry.filePath).filename()),
               static_cast<int>(bounds.w - 272), 2),
       2, {255, 255, 255, 255});
  rounded(renderer, x + 51, y + 256, bounds.w - 102, 66, 14,
          removeRom ? SDL_Color{120, 214, 148, 20}
                    : SDL_Color{255, 98, 94, 20});
  text(renderer, x + 100, y + 273,
       tr(removeRom ? TextId::SaveWillRemain : TextId::RomWillRemain, language),
       2,
       removeRom ? SDL_Color{188, 234, 200, 255}
                 : SDL_Color{255, 185, 180, 255});
  text(renderer, x + 100, y + 296,
       tr(removeRom ? TextId::SaveRemainDescription
                    : TextId::RomRemainDescription,
          language),
       1, {143, 166, 151, 255});

  drawChoiceButtons(renderer, layout.confirmButtons,
                    {tr(TextId::Cancel, language),
                     tr(removeRom ? TextId::RemoveRom : TextId::DeleteSaveData,
                        language)}, confirmationChoice, accent,
                    {255, 98, 94, 34}, {255, 170, 166, 255});
  centeredText(renderer, x + 396, y + 437,
               tr(TextId::SafeDefaultHint, language), 1,
               {122, 135, 159, 255});
}

std::string playTimeText(std::uint64_t seconds) {
  const std::uint64_t hours = seconds / 3600;
  const std::uint64_t minutes = (seconds % 3600) / 60;
  if (hours > 0)
    return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
  return std::to_string(minutes) + "m";
}

std::string lastPlayedText(std::int64_t timestamp, i18n::Language language) {
  if (timestamp <= 0)
    return tr(TextId::NeverPlayed, language);
  const std::time_t value = static_cast<std::time_t>(timestamp);
  const std::tm *local = std::localtime(&value);
  if (local == nullptr)
    return tr(TextId::NeverPlayed, language);
  std::ostringstream output;
  output << std::put_time(local, "%Y-%m-%d %H:%M");
  return output.str();
}

void drawInfo(SDL_Renderer *renderer, const library::GameEntry &entry,
              bool saveDataPresent, i18n::Language language, SDL_Color accent) {
  const ManagementLayout &layout = managementLayout();
  const Rect &info = layout.info;
  const int x = static_cast<int>(info.x);
  const int y = static_cast<int>(info.y);
  const int fileWidth = static_cast<int>(info.w - 104);
  fill(renderer, 0, 0, kCanvasWidth, kCanvasHeight, {2, 5, 12, 205});
  rounded(renderer, info.x, info.y, info.w, info.h, 32, {8, 16, 31, 255});
  text(renderer, x + 48, y + 40, tr(TextId::GameInfo, language), 4,
       {255, 255, 255, 255});
  text(renderer, x + 48, y + 85, tr(TextId::Title, language), 1,
       {132, 145, 169, 255});
  text(renderer, x + 48, y + 107, fitText(entry.title, fileWidth - 8, 3), 3,
       accent);
  text(renderer, x + 48, y + 154, tr(TextId::File, language), 1,
       {132, 145, 169, 255});
  text(renderer, x + 48, y + 176,
       fitText(paths::toUtf8(paths::fromUtf8(entry.filePath).filename()),
               fileWidth, 2),
       2, {235, 239, 247, 255});

  text(renderer, x + 48, y + 222, tr(TextId::PlayTime, language), 1,
       {132, 145, 169, 255});
  text(renderer, x + 48, y + 245, playTimeText(entry.playSeconds), 2,
       {235, 239, 247, 255});
  text(renderer, x + 348, y + 222, tr(TextId::LastPlayed, language), 1,
       {132, 145, 169, 255});
  text(renderer, x + 348, y + 245, lastPlayedText(entry.lastPlayed, language),
       2, {235, 239, 247, 255});

  text(renderer, x + 48, y + 291, tr(TextId::FavoriteStatus, language), 1,
       {132, 145, 169, 255});
  text(renderer, x + 48, y + 314,
       tr(entry.favorite ? TextId::On : TextId::Off, language), 2,
       entry.favorite ? accent : SDL_Color{180, 190, 210, 255});
  text(renderer, x + 348, y + 291, tr(TextId::SaveData, language), 1,
       {132, 145, 169, 255});
  text(renderer, x + 348, y + 314,
       tr(saveDataPresent ? TextId::SaveDataPresent : TextId::SaveDataMissing,
          language),
       2,
       saveDataPresent ? SDL_Color{132, 220, 157, 255}
                       : SDL_Color{180, 190, 210, 255});
  drawButton(renderer, layout.infoBack, 18, tr(TextId::Back, language), 2,
             accent, {23, 23, 38, 255}, true);
}

} // namespace

void drawGameManagementOverlay(SDL_Renderer *renderer,
                               const library::GameEntry &entry,
                               int selectedAction, ManagementDialog dialog,
                               int confirmationChoice, bool saveDataPresent,
                               i18n::Language language, SDL_Color accent) {
  fill(renderer, 0, 0, kCanvasWidth, kCanvasHeight, {3, 7, 16, 65});
  drawMenu(renderer, entry, selectedAction, saveDataPresent, language, accent);
  if (dialog == ManagementDialog::ConfirmRemoveRom ||
      dialog == ManagementDialog::ConfirmDeleteSave) {
    drawConfirmation(renderer, entry, dialog, confirmationChoice, language,
                     accent);
  } else if (dialog == ManagementDialog::Info) {
    drawInfo(renderer, entry, saveDataPresent, language, accent);
  }
}

int managementActionAt(float x, float y) {
  return managementLayout().actions.hit(x, y);
}

int confirmationChoiceAt(float x, float y) {
  return managementLayout().confirmButtons.hit(x, y);
}

bool managementInfoBackAt(float x, float y) {
  return managementLayout().infoBack.contains(x, y);
}

} // namespace launcher::ui
