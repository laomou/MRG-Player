#include "ui/GameManagementScreen.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

#include "game/GamePlatform.hpp"
#include "i18n/Localization.hpp"
#include "ui/Primitives.hpp"

namespace launcher::ui {
namespace {

using i18n::TextId;
using i18n::tr;

constexpr float kPanelX = 982.0f;
constexpr float kPanelY = 48.0f;
constexpr float kActionX = 1014.0f;
constexpr float kActionY = 288.0f;
constexpr float kActionWidth = 482.0f;
constexpr float kActionHeight = 68.0f;
constexpr float kActionStep = 80.0f;

float actionTop(int index) {
  return kActionY + index * kActionStep + (index >= 4 ? 18.0f : 0.0f);
}

std::string fitLine(std::string value, int width, int scale) {
  if (textWidth(value, scale) <= width)
    return value;
  const std::string suffix = "...";
  while (!value.empty() && textWidth(value + suffix, scale) > width) {
    const unsigned char last = static_cast<unsigned char>(value.back());
    value.pop_back();
    while (!value.empty() && (last & 0xc0) == 0x80 &&
           (static_cast<unsigned char>(value.back()) & 0xc0) == 0x80) {
      value.pop_back();
    }
    if (!value.empty() &&
        (static_cast<unsigned char>(value.back()) & 0xc0) == 0xc0) {
      value.pop_back();
    }
  }
  return value + suffix;
}

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
              const config::InputPrompts &prompts) {
  rounded(renderer, kPanelX - 8, kPanelY + 8, 562, 804, 28, {0, 0, 0, 42});
  rounded(renderer, kPanelX, kPanelY, 546, 804, 28, {12, 21, 37, 232});
  text(renderer, 1014, 84, tr(TextId::ManageGame, language), 3,
       {246, 248, 252, 255});
  text(renderer, 1014, 132, fitLine(entry.title, 482, 2), 2,
       {255, 220, 114, 255});
  const auto &platform = game::platformDescriptor(entry.platform);
  text(renderer, 1014, 171,
       std::string(platform.displayName) + "  /  " +
           tr(saveDataPresent ? TextId::SaveDataPresent
                              : TextId::SaveDataMissing,
              language),
       1, {180, 195, 213, 255});
  fill(renderer, 1014, 213, 482, 1, {255, 255, 255, 30});
  text(renderer, 1014, 239, tr(TextId::ChooseManagementAction, language), 1,
       {164, 180, 201, 255});

  // Frequent actions first; destructive actions are separated at the bottom.
  const std::array<TextId, kManagementActionCount> labels{
      TextId::Start,
      entry.favorite ? TextId::Unfavorite : TextId::Favorite,
      TextId::GameInfo,
      TextId::Cancel,
      TextId::RemoveFromLibrary,
      TextId::DeleteSaveData};
  const std::array<TextId, kManagementActionCount> descriptions{
      TextId::StartDescription,
      TextId::FavoriteDescription,
      TextId::GameInfoDescription,
      TextId::CancelDescription,
      TextId::RemoveRomDescription,
      saveDataPresent ? TextId::DeleteSaveDescription : TextId::NoSaveData};
  const std::array<std::string, kManagementActionCount> glyphs{
      prompts.confirm, "*", "i", prompts.back, "-", "!"};
  for (int index = 0; index < kManagementActionCount; ++index) {
    const float y = actionTop(index);
    const bool selected = index == selectedAction;
    const bool destructive = index >= 4;
    const SDL_Color accent = destructive ? SDL_Color{255, 157, 150, 255}
                                         : SDL_Color{255, 211, 101, 255};
    rounded(renderer, kActionX, y, kActionWidth, kActionHeight, 12,
            selected      ? accent
            : destructive ? SDL_Color{255, 108, 104, 12}
                          : SDL_Color{255, 255, 255, 12});
    drawButtonGlyph(renderer, 1052, static_cast<int>(y + 34), glyphs[index],
                    selected ? SDL_Color{23, 23, 39, 255}
                             : SDL_Color{255, 255, 255, 18},
                    selected ? accent : SDL_Color{205, 213, 230, 255});
    text(renderer, 1090, static_cast<int>(y + 8), tr(labels[index], language),
         2,
         selected      ? SDL_Color{20, 24, 35, 255}
         : destructive ? accent
                       : SDL_Color{241, 245, 251, 255});
    text(renderer, 1090, static_cast<int>(y + 39),
         fitLine(tr(descriptions[index], language), 390, 1), 1,
         selected ? SDL_Color{70, 57, 39, 255} : SDL_Color{165, 181, 204, 255});
  }
  fill(renderer, 1014, 608, 482, 1, {255, 157, 150, 36});
  text(renderer, 1014, 807, tr(TextId::RomAndSaveSeparate, language), 1,
       {165, 181, 204, 255});
}

void drawConfirmation(SDL_Renderer *renderer, const library::GameEntry &entry,
                      ManagementDialog dialog, int confirmationChoice,
                      i18n::Language language) {
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {2, 5, 12, 205});
  rounded(renderer, 305, 150, 990, 600, 34, {8, 16, 31, 255});
  fill(renderer, 305, 150, 990, 9, {255, 205, 99, 255});
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
       fitLine(std::filesystem::path(entry.filePath).filename().string(), 650,
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

  const std::array<TextId, 2> labels{
      TextId::Cancel, removeRom ? TextId::RemoveRom : TextId::DeleteSaveData};
  for (int choice = 0; choice < 2; ++choice) {
    const float x = 369.0f + choice * 450.0f;
    const bool selected = choice == confirmationChoice;
    rounded(renderer, x, 595, 412, 66, 18,
            selected ? SDL_Color{255, 211, 101, 255}
                     : SDL_Color{255, 98, 94, 34});
    centeredText(renderer, static_cast<int>(x + 206), 615,
                 tr(labels[choice], language), 2,
                 selected ? SDL_Color{23, 23, 38, 255}
                          : SDL_Color{255, 170, 166, 255});
  }
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
              bool saveDataPresent, i18n::Language language) {
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {2, 5, 12, 205});
  rounded(renderer, 355, 145, 890, 610, 32, {8, 16, 31, 255});
  text(renderer, 415, 195, tr(TextId::GameInfo, language), 4,
       {255, 255, 255, 255});
  text(renderer, 415, 251, tr(TextId::Title, language), 1,
       {132, 145, 169, 255});
  text(renderer, 415, 279, fitLine(entry.title, 760, 3), 3,
       {255, 220, 114, 255});
  text(renderer, 415, 337, tr(TextId::File, language), 1, {132, 145, 169, 255});
  text(renderer, 415, 365,
       fitLine(std::filesystem::path(entry.filePath).filename().string(), 760,
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
       entry.favorite ? SDL_Color{255, 220, 114, 255}
                      : SDL_Color{180, 190, 210, 255});
  text(renderer, 790, 509, tr(TextId::SaveData, language), 1,
       {132, 145, 169, 255});
  text(renderer, 790, 537,
       tr(saveDataPresent ? TextId::SaveDataPresent : TextId::SaveDataMissing,
          language),
       2,
       saveDataPresent ? SDL_Color{132, 220, 157, 255}
                       : SDL_Color{180, 190, 210, 255});
  rounded(renderer, 415, 620, 770, 68, 18, {255, 211, 101, 255});
  centeredText(renderer, 800, 642, tr(TextId::Back, language), 2,
               {23, 23, 38, 255});
}

} // namespace

void drawGameManagementOverlay(SDL_Renderer *renderer,
                               const library::GameEntry &entry,
                               int selectedAction, ManagementDialog dialog,
                               int confirmationChoice, bool saveDataPresent,
                               i18n::Language language,
                               const config::InputPrompts &prompts) {
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {3, 7, 16, 65});
  drawMenu(renderer, entry, selectedAction, saveDataPresent, language, prompts);
  if (dialog == ManagementDialog::ConfirmRemoveRom ||
      dialog == ManagementDialog::ConfirmDeleteSave) {
    drawConfirmation(renderer, entry, dialog, confirmationChoice, language);
  } else if (dialog == ManagementDialog::Info) {
    drawInfo(renderer, entry, saveDataPresent, language);
  }
}

int managementActionAt(float x, float y) {
  if (x < kActionX || x > kActionX + kActionWidth)
    return -1;
  for (int index = 0; index < kManagementActionCount; ++index) {
    const float top = actionTop(index);
    if (y >= top && y <= top + kActionHeight)
      return index;
  }
  return -1;
}

int confirmationChoiceAt(float x, float y) {
  if (y < 595 || y > 661)
    return -1;
  if (x >= 369 && x <= 781)
    return 0;
  if (x >= 819 && x <= 1231)
    return 1;
  return -1;
}

} // namespace launcher::ui
