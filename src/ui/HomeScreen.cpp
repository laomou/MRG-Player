#include "ui/HomeScreen.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "game/GamePlatform.hpp"
#include "i18n/Localization.hpp"
#include "model/Game.hpp"
#include "ui/Primitives.hpp"

namespace launcher::ui {
namespace {

const Game &styleFor(int index) {
  return games()[static_cast<std::size_t>(index) % kGameCount];
}

// The row never moves: every slot keeps its place and only the highlight
// travels, so the first game always sits against the left edge.
constexpr float kRowLeft = 72.0f;
constexpr float kRowRight = 1528.0f;
constexpr float kRowBottom = 678.0f;
constexpr float kCardAspect = 314.0f / 226.0f;
constexpr float kCardGap = 20.0f;
constexpr float kMaximumCardWidth = 226.0f;

struct CardBox {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// The shelf never changes its card count: seven fit the width, and a library
// with more games slides the row instead, leaving the next card hanging off the
// right edge as the cue that there is more.
constexpr int kVisibleSlots = 7;

float slotPitch() {
  return (kRowRight - kRowLeft) / static_cast<float>(kVisibleSlots);
}

CardBox cardBox(float column, bool selected) {
  const float base =
      std::min(kMaximumCardWidth, slotPitch() - kCardGap);
  const float scale = selected ? 1.04f : 1.0f;
  const float width = base * scale;
  const float height = base * kCardAspect * scale;
  const float centre = kRowLeft + slotPitch() * (column + 0.5f);
  return {centre - width * 0.5f, kRowBottom - height, width, height};
}

std::string fitTitle(std::string title, int maxWidth, int scale) {
  if (textWidth(title, scale) <= maxWidth)
    return title;
  const std::string suffix = "...";
  while (!title.empty() && textWidth(title + suffix, scale) > maxWidth) {
    const unsigned char last = static_cast<unsigned char>(title.back());
    title.pop_back();
    while (!title.empty() && (last & 0xc0) == 0x80 &&
           (static_cast<unsigned char>(title.back()) & 0xc0) == 0x80) {
      title.pop_back();
    }
    if (!title.empty() &&
        (static_cast<unsigned char>(title.back()) & 0xc0) == 0xc0) {
      title.pop_back();
    }
  }
  return title + suffix;
}

void drawCard(SDL_Renderer *renderer, int index, float column, bool selected,
              float pulse, const library::GameEntry *entry) {
  const Game &style = styleFor(index);
  const CardBox box = cardBox(column, selected);
  const float x = box.x;
  // The selected card sits still: the highlight does the work. A vertical
  // float here only moved it a fraction of a pixel per frame, which the
  // rasteriser turned into a one-pixel hop every few frames.
  const float y = box.y;
  const float width = box.width;
  const float height = box.height;
  const Uint8 alpha = selected ? 255 : 150;
  const float unit = width / kMaximumCardWidth;
  const float radius = (selected ? 22.0f : 18.0f) * unit;

  if (selected) {
    const float glow = std::floor(9.0f * unit);
    rounded(renderer, x - glow, y - glow, width + glow * 2.0f,
            height + glow * 2.0f, radius + glow,
            {style.accent.r, style.accent.g, style.accent.b,
             static_cast<Uint8>(55 + pulse * 35)});
    const float ring = 3.0f;
    rounded(renderer, x - ring, y - ring, width + ring * 2.0f,
            height + ring * 2.0f, radius + ring,
            {style.accent.r, style.accent.g, style.accent.b, 255});
  }

  if (entry == nullptr) {
    rounded(renderer, x, y, width, height, radius, {13, 20, 36, 255});
    rounded(renderer, x + 1, y + 1, width - 2, height - 2, radius - 1,
            {19, 28, 48, 255});
    return;
  }

  for (int row = 0; row < static_cast<int>(height); ++row) {
    const float t = static_cast<float>(row) / height;
    const float distance = std::min(row + 0.5f, height - row - 0.5f);
    const float yy = std::max(0.0f, radius - distance);
    const float inset =
        radius - std::sqrt(std::max(0.0f, radius * radius - yy * yy));
    fill(renderer, x + inset, y + row, width - 2 * inset, 1,
         mix({style.top.r, style.top.g, style.top.b, alpha},
             {style.bottom.r, style.bottom.g, style.bottom.b, alpha}, t));
  }

  const float margin = 12.0f * unit;
  if (entry->favorite) {
    const float badge = 28.0f * unit;
    circle(renderer, static_cast<int>(x + width - badge),
           static_cast<int>(y + badge), 16.0f * unit,
           {255, 220, 114, static_cast<Uint8>(alpha * 0.9f)});
    centeredText(renderer, static_cast<int>(x + width - badge),
                 static_cast<int>(y + badge - 7.0f * unit), "*", 2,
                 {18, 18, 33, alpha});
  }

  const float panelY = y + height * 0.60f;
  rounded(renderer, x + margin, panelY, width - margin * 2.0f,
          height * 0.40f - margin, 12.0f * unit,
          {6, 9, 26, static_cast<Uint8>(alpha * 0.78f)});
  circle(renderer, static_cast<int>(x + width * 0.5f),
         static_cast<int>(y + height * 0.32f), width * 0.142f,
         {style.accent.r, style.accent.g, style.accent.b,
          static_cast<Uint8>(alpha * 0.85f)});
  const auto &platform = game::platformDescriptor(entry->platform);
  const int scale = selected && width >= 240.0f ? 3 : 2;
  centeredText(renderer, static_cast<int>(x + width * 0.5f),
               static_cast<int>(y + height * 0.29f),
               std::string(platform.displayName), scale, {8, 14, 30, alpha});

  centeredText(renderer, static_cast<int>(x + width * 0.5f),
               static_cast<int>(y + height * 0.70f),
               fitTitle(entry->title, static_cast<int>(width - margin * 2.0f),
                        scale),
               scale, {255, 255, 255, alpha});
  centeredText(renderer, static_cast<int>(x + width * 0.5f),
               static_cast<int>(y + height * 0.84f),
               std::string(platform.fileTypeLabel), scale,
               {style.accent.r, style.accent.g, style.accent.b, alpha});
}

// Footer geometry lives here because the drawing and the hit test have to agree
// on it; the metrics themselves come from the hint helpers.
constexpr float kFooterRight = 1528.0f;
constexpr float kFooterY = 822.0f;

std::vector<Hint> footerHints(bool hasGame, i18n::Language language,
                              const config::InputPrompts &prompts) {
  std::vector<Hint> hints;
  if (hasGame) {
    hints.push_back(
        {i18n::tr(i18n::TextId::Start, language), prompts.confirm, true});
    hints.push_back({i18n::tr(i18n::TextId::ManageGame, language),
                     prompts.manage, false});
  }
  hints.push_back(
      {i18n::tr(i18n::TextId::Quit, language), prompts.back, false});
  return hints;
}

} // namespace

void drawHome(SDL_Renderer *renderer, int selectedGame, float time,
              Uint64 toastUntil, std::string_view toastMessage, int startupPage,
              i18n::Language language, const library::GameLibrary &gameLibrary,
              const config::InputPrompts &prompts, HomeFocus focus,
              int focusedPill, int focusedAction, float rowOffset) {
  using i18n::TextId;
  using i18n::tr;
  const int slotCount = static_cast<int>(gameLibrary.slotCount());
  selectedGame = std::clamp(selectedGame, 0, slotCount - 1);
  const Game &style = styleFor(selectedGame);
  const bool libraryEmpty = gameLibrary.entries().empty();
  const library::GameEntry *selectedEntry =
      selectedGame < static_cast<int>(gameLibrary.entries().size())
          ? &gameLibrary.entries()[static_cast<std::size_t>(selectedGame)]
          : nullptr;

  drawBackground(renderer, style, time);
  rounded(renderer, 72, 57, 4, 32, 2, style.accent);
  text(renderer, 92, 54, "MRG PLAYER", 3, {255, 255, 255, 255});
  text(renderer, 92, 99, "RETRO GAMES. TIMELESS PLAY.", 1,
       {167, 184, 207, 255});
  // The pills are buttons too, so they take the same ring when the direction
  // keys are on them.
  if (focus == HomeFocus::Pills) {
    const SDL_FRect pill = focusedPill == 0 ? SDL_FRect{1110, 52, 246, 58}
                                            : SDL_FRect{1372, 52, 156, 58};
    rounded(renderer, pill.x - 3.0f, pill.y - 3.0f, pill.w + 6.0f,
            pill.h + 6.0f, 32.0f, style.accent);
  }
  rounded(renderer, 1110, 52, 246, 58, 29, {4, 10, 26, 145});
  circle(renderer, 1143, 81, 15, style.accent);
  text(renderer, 1136, 66, "+", 2, {18, 18, 33, 255});
  text(renderer, 1170, 66, tr(TextId::Library, language), 2,
       {255, 255, 255, 255});
  text(renderer, 1320, 71, std::to_string(gameLibrary.entries().size()), 1,
       {191, 201, 220, 255});
  rounded(renderer, 1372, 52, 156, 58, 29, {4, 10, 26, 175});
  circle(renderer, 1405, 81, 15, {255, 255, 255, 45});
  text(renderer, 1399, 66, "?", 2, style.accent);
  text(renderer, 1431, 66, tr(TextId::Settings, language), 2,
       {255, 255, 255, 255});

  const TextId sectionTitle =
      libraryEmpty
          ? TextId::GameLibrary
          : (startupPage == 0 ? TextId::ContinuePlaying : TextId::GameLibrary);
  const TextId sectionSubtitle =
      libraryEmpty ? TextId::ChooseRom
                   : (startupPage == 0 ? TextId::ContinueSubtitle
                                       : TextId::LibrarySubtitle);
  text(renderer, 72, 174, tr(sectionTitle, language), 1, style.accent);
  text(renderer, 72, 207, tr(TextId::ChooseYourGame, language), 3,
       {255, 255, 255, 255});
  text(renderer, 72, 251, tr(sectionSubtitle, language), 1,
       {191, 201, 220, 255});

  const float pulse = (std::sin(time * 3.0f) + 1.0f) * 0.5f;
  // Draw one column beyond each edge so a card sliding out and the next one
  // sliding in both stay whole while the row travels.
  const int firstColumn = static_cast<int>(std::floor(rowOffset));
  const int lastSlot =
      std::min(slotCount - 1, firstColumn + kVisibleSlots + 1);
  for (int i = std::max(0, firstColumn - 1); i <= lastSlot; ++i) {
    if (i == selectedGame)
      continue;
    const library::GameEntry *entry =
        i < static_cast<int>(gameLibrary.entries().size())
            ? &gameLibrary.entries()[static_cast<std::size_t>(i)]
            : nullptr;
    drawCard(renderer, i, static_cast<float>(i) - rowOffset, false, pulse,
             entry);
  }
  drawCard(renderer, selectedGame,
           static_cast<float>(selectedGame) - rowOffset, true, pulse,
           selectedEntry);

  if (selectedEntry != nullptr) {
    centeredText(renderer, 800, 711, fitTitle(selectedEntry->title, 760, 3), 3,
                 style.accent);
    const std::uint64_t hours = selectedEntry->playSeconds / 3600;
    const std::uint64_t minutes = (selectedEntry->playSeconds % 3600) / 60;
    std::string playTime = tr(TextId::PlayTime, language);
    playTime += ": ";
    if (hours > 0)
      playTime += std::to_string(hours) + "h ";
    playTime += std::to_string(minutes) + "m";
    centeredText(renderer, 800, 758, playTime, 1, {232, 236, 244, 255});
  }

  fill(renderer, 72, 798, 1456, 1, {255, 255, 255, 24});
  text(renderer, 72, 828, prompts.horizontal, 1, {168, 184, 207, 255});
  const std::vector<Hint> footer =
      footerHints(selectedEntry != nullptr, language, prompts);
  const int focused =
      focus == HomeFocus::Actions
          ? std::clamp(focusedAction, 0, static_cast<int>(footer.size()) - 1)
          : -1;
  float cursor =
      kFooterRight - static_cast<float>(hintRowWidth(footer));
  for (int index = 0; index < static_cast<int>(footer.size()); ++index) {
    if (index == focused)
      drawHintFocus(renderer, cursor, kFooterY, footer[index], style.accent);
    drawHint(renderer, cursor, kFooterY, footer[index], style.accent);
    cursor += static_cast<float>(hintWidth(footer[index])) + kHintGap;
  }

  if (SDL_GetTicks64() < toastUntil && !toastMessage.empty()) {
    rounded(renderer, 484, 760, 632, 66, 18, {4, 8, 22, 235});
    centeredText(renderer, 800, 779, std::string(toastMessage), 2,
                 style.accent);
  }
}

int homeActionCount(bool hasGame) { return hasGame ? 3 : 1; }

int homeActionAt(float x, float y, bool hasGame, i18n::Language language,
                 const config::InputPrompts &prompts) {
  if (y < kFooterY - 12.0f || y > kFooterY + kHintChipHeight + 14.0f)
    return -1;
  const std::vector<Hint> hints = footerHints(hasGame, language, prompts);
  float cursor = kFooterRight - static_cast<float>(hintRowWidth(hints));
  for (int index = 0; index < static_cast<int>(hints.size()); ++index) {
    const float width = static_cast<float>(hintWidth(hints[index]));
    if (x >= cursor - 10.0f && x <= cursor + width + 10.0f)
      return index;
    cursor += width + kHintGap;
  }
  return -1;
}

int homePillAt(float x, float y) {
  if (y < 49.0f || y > 113.0f)
    return -1;
  if (x >= 1107.0f && x <= 1359.0f)
    return 0;
  if (x >= 1369.0f && x <= 1531.0f)
    return 1;
  return -1;
}

int homeCardAt(float x, float y, int count, float rowOffset) {
  // The selected card is drawn a little larger, so accept either size and a
  // click on its rim still counts.
  const int firstColumn = static_cast<int>(std::floor(rowOffset));
  const int lastSlot = std::min(count - 1, firstColumn + kVisibleSlots + 1);
  for (int slot = std::max(0, firstColumn - 1); slot <= lastSlot; ++slot) {
    for (const bool selected : {false, true}) {
      const CardBox box = cardBox(static_cast<float>(slot) - rowOffset,
                                  selected);
      if (x >= box.x && x <= box.x + box.width && y >= box.y &&
          y <= box.y + box.height)
        return slot;
    }
  }
  return -1;
}

float homeRowTarget(int selectedGame, int count) {
  const int overflow = std::max(0, count - kVisibleSlots);
  const int start = std::clamp(selectedGame - kVisibleSlots + 1, 0, overflow);
  return static_cast<float>(start);
}

void moveSelection(int &selectedGame, int direction, int count) {
  if (count <= 0)
    return;
  selectedGame = (selectedGame + direction + count) % count;
}

} // namespace launcher::ui
