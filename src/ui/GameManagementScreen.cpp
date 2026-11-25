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

const PanelLayout kPanel = panelLayout(
    {982, 48, 546, 804}, Rect{982, 48, 546, 804}.inset(32, 36, 32, 20),
    129, 26, 16);
const LinearLayout kActions{{kPanel.content.x, 248, kPanel.content.w, 68},
                            kManagementActionCount, 18, Axis::Vertical, 4, 18};
const Rect kConfirmBounds{305, 150, 990, 600};
const LinearLayout kConfirmButtons = equalColumns(
    {kConfirmBounds.x + 64, kConfirmBounds.y + 445,
     kConfirmBounds.w - 128, 66}, 2, 38);
const Rect kInfoBounds{355, 145, 890, 610};
const Rect kInfoBack{kInfoBounds.x + 60, kInfoBounds.bottom() - 135,
                    kInfoBounds.w - 120, 68};

void drawButtonGlyph(SDL_Renderer *renderer, int x, int y,
                     const std::string &glyph, SDL_Color color,
                     SDL_Color glyphColor) {
  const int scale = textWidth(glyph, 2) <= 28 ? 2 : 1;
  const int width = std::max(34, textWidth(glyph, scale) + 12);
  rounded(renderer, x - width / 2, y - 17, width, 34, 17, color);
  centeredText(renderer, x, y - (scale == 2 ? 13 : 9), glyph, scale,
               glyphColor);
}

void drawMenu(SDL_Renderer *renderer, const library::GameEntry &entry,
              int selectedAction, bool saveDataPresent, i18n::Language language,
              const config::InputPrompts &prompts, SDL_Color themeAccent) {
  drawPanel(renderer, kPanel.bounds, 28, {12, 21, 37, 232},
            kPanel.bounds.expanded(8, 0).translated(0, 8), {0, 0, 0, 42});
  text(renderer, 1014, 84, tr(TextId::ManageGame, language), 3,
       {246, 248, 252, 255});
  text(renderer, 1014, 132, fitText(entry.title, 482, 2), 2, themeAccent);
  const auto &platform = game::platformDescriptor(entry.platform);
  text(renderer, 1014, 171,
       std::string(platform.displayName) + "  /  " +
           tr(saveDataPresent ? TextId::SaveDataPresent
                              : TextId::SaveDataMissing,
              language),
       1, {180, 195, 213, 255});
  fill(renderer, kPanel.header.x, kPanel.header.bottom(), kPanel.header.w, 1,
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
  const std::array<std::string, kManagementActionCount> glyphs{
      prompts.confirm, "*", "i", prompts.back, "-", "!"};
  for (int index = 0; index < kManagementActionCount; ++index) {
    const Rect bounds = kActions.item(index);
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
    drawButtonGlyph(renderer, 1052, static_cast<int>(y + 34), glyphs[index],
                    selected ? SDL_Color{23, 23, 39, 255}
                             : SDL_Color{255, 255, 255, 18},
                    selected ? rowAccent : SDL_Color{205, 213, 230, 255});
    text(renderer, 1090,
         static_cast<int>(y + (descriptions[index].empty() ? 24 : 8)),
         tr(labels[index], language), 2,
         selected      ? SDL_Color{20, 24, 35, 255}
         : destructive ? rowAccent
                       : SDL_Color{241, 245, 251, 255});
    if (!descriptions[index].empty()) {
      text(renderer, 1090, static_cast<int>(y + 39),
           fitText(descriptions[index], 390, 1), 1,
           selected ? SDL_Color{38, 48, 54, 255}
                    : SDL_Color{165, 181, 204, 255});
    }
  }
  fill(renderer, kPanel.content.x, kActions.item(4).y - 18,
       kPanel.content.w, 1, {255, 157, 150, 36});
  // The legend belongs to the drawer, not to the shelf it covers.
  fill(renderer, kPanel.footer.x, kPanel.footer.y - 16, kPanel.footer.w, 1,
       {255, 255, 255, 30});
  const std::vector<Hint> hints{
      {tr(TextId::Back, language), prompts.back, false},
      {tr(TextId::ConfirmControl, language), prompts.confirm, true}};
  drawHintRow(renderer, kPanel.footer.right(), kPanel.footer.y, hints,
              themeAccent);
}

void drawConfirmation(SDL_Renderer *renderer, const library::GameEntry &entry,
                      ManagementDialog dialog, int confirmationChoice,
                      i18n::Language language, SDL_Color accent) {
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {2, 5, 12, 205});
  rounded(renderer, kConfirmBounds.x, kConfirmBounds.y, kConfirmBounds.w,
          kConfirmBounds.h, 34, {8, 16, 31, 255});
  fill(renderer, kConfirmBounds.x, kConfirmBounds.y, kConfirmBounds.w, 9, accent);
  circle(renderer, 409, 254, 40, {255, 98, 94, 36});
  centeredText(renderer, 409, 243, "!", 4, {255, 133, 128, 255});
  const bool removeRom = dialog == ManagementDialog::ConfirmRemoveRom;
  text(renderer, 477, 215,
       tr(removeRom ? TextId::RemoveGame : TextId::DeleteSaveData, language), 2,
       {255, 143, 136, 255});
  text(renderer, 477, 258,
       tr(removeRom ? TextId::RemoveConfirmTitle
                    : TextId::DeleteSaveConfirmTitle,
          language),
       4, {255, 255, 255, 255});
  text(renderer, 369, 335,
       tr(removeRom ? TextId::RemoveConfirmDescription
                    : TextId::DeleteSaveConfirmDescription,
          language),
       2, {224, 229, 239, 255});
  rounded(renderer, 369, 362, 862, 86, 18, {255, 255, 255, 14});
  text(renderer, 394, 382, tr(TextId::File, language), 1, {132, 145, 169, 255});
  text(renderer, 394, 411,
       fitText(paths::toUtf8(paths::fromUtf8(entry.filePath).filename()), 650,
               2),
       2, {255, 255, 255, 255});
  rounded(renderer, 369, 470, 862, 82, 18,
          removeRom ? SDL_Color{120, 214, 148, 20}
                    : SDL_Color{255, 98, 94, 20});
  text(renderer, 430, 491,
       tr(removeRom ? TextId::SaveWillRemain : TextId::RomWillRemain, language),
       2,
       removeRom ? SDL_Color{188, 234, 200, 255}
                 : SDL_Color{255, 185, 180, 255});
  text(renderer, 430, 520,
       tr(removeRom ? TextId::SaveRemainDescription
                    : TextId::RomRemainDescription,
          language),
       1, {143, 166, 151, 255});

  drawChoiceButtons(renderer, kConfirmButtons,
                    {tr(TextId::Cancel, language),
                     tr(removeRom ? TextId::RemoveRom : TextId::DeleteSaveData,
                        language)}, confirmationChoice, accent,
                    {255, 98, 94, 34}, {255, 170, 166, 255});
  centeredText(renderer, 800, 696, tr(TextId::SafeDefaultHint, language), 1,
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
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {2, 5, 12, 205});
  rounded(renderer, kInfoBounds.x, kInfoBounds.y, kInfoBounds.w,
          kInfoBounds.h, 32, {8, 16, 31, 255});
  text(renderer, 415, 195, tr(TextId::GameInfo, language), 4,
       {255, 255, 255, 255});
  text(renderer, 415, 251, tr(TextId::Title, language), 1,
       {132, 145, 169, 255});
  text(renderer, 415, 279, fitText(entry.title, 760, 3), 3, accent);
  text(renderer, 415, 337, tr(TextId::File, language), 1, {132, 145, 169, 255});
  text(renderer, 415, 365,
       fitText(paths::toUtf8(paths::fromUtf8(entry.filePath).filename()), 760,
               2),
       2, {235, 239, 247, 255});

  text(renderer, 415, 423, tr(TextId::PlayTime, language), 1,
       {132, 145, 169, 255});
  text(renderer, 415, 451, playTimeText(entry.playSeconds), 2,
       {235, 239, 247, 255});
  text(renderer, 790, 423, tr(TextId::LastPlayed, language), 1,
       {132, 145, 169, 255});
  text(renderer, 790, 451, lastPlayedText(entry.lastPlayed, language), 2,
       {235, 239, 247, 255});

  text(renderer, 415, 509, tr(TextId::FavoriteStatus, language), 1,
       {132, 145, 169, 255});
  text(renderer, 415, 537,
       tr(entry.favorite ? TextId::On : TextId::Off, language), 2,
       entry.favorite ? accent : SDL_Color{180, 190, 210, 255});
  text(renderer, 790, 509, tr(TextId::SaveData, language), 1,
       {132, 145, 169, 255});
  text(renderer, 790, 537,
       tr(saveDataPresent ? TextId::SaveDataPresent : TextId::SaveDataMissing,
          language),
       2,
       saveDataPresent ? SDL_Color{132, 220, 157, 255}
                       : SDL_Color{180, 190, 210, 255});
  drawButton(renderer, kInfoBack, 18, tr(TextId::Back, language), 2,
             accent, {23, 23, 38, 255}, true);
}

} // namespace

void drawGameManagementOverlay(SDL_Renderer *renderer,
                               const library::GameEntry &entry,
                               int selectedAction, ManagementDialog dialog,
                               int confirmationChoice, bool saveDataPresent,
                               i18n::Language language,
                               const config::InputPrompts &prompts,
                               SDL_Color accent) {
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {3, 7, 16, 65});
  drawMenu(renderer, entry, selectedAction, saveDataPresent, language, prompts,
           accent);
  if (dialog == ManagementDialog::ConfirmRemoveRom ||
      dialog == ManagementDialog::ConfirmDeleteSave) {
    drawConfirmation(renderer, entry, dialog, confirmationChoice, language,
                     accent);
  } else if (dialog == ManagementDialog::Info) {
    drawInfo(renderer, entry, saveDataPresent, language, accent);
  }
}

int managementActionAt(float x, float y) {
  return kActions.hit(x, y);
}

int confirmationChoiceAt(float x, float y) {
  return kConfirmButtons.hit(x, y);
}

bool managementInfoBackAt(float x, float y) {
  return kInfoBack.contains(x, y);
}

} // namespace launcher::ui
