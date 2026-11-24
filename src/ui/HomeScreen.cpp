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

// The two header pills are sized from their labels and anchored against the
// right margin, so a longer translation never runs past the edge of the
// canvas; with the current Chinese labels the pair keeps its old geometry.
SDL_FRect pillRect(bool settings, i18n::Language language, std::size_t count) {
  using i18n::TextId;
  using i18n::tr;
  const int libraryLabel = textWidth(tr(TextId::Library, language), 2);
  const int settingsLabel = textWidth(tr(TextId::Settings, language), 2);
  const int countLabel = textWidth(std::to_string(count), 1);
  const float settingsWidth =
      std::max(156.0f, 44.0f + static_cast<float>(settingsLabel) + 20.0f);
  const float libraryWidth =
      std::max(246.0f, 44.0f + static_cast<float>(libraryLabel) + 16.0f +
                           static_cast<float>(countLabel) + 20.0f);
  const float settingsX = kRowRight - settingsWidth;
  if (settings)
    return SDL_FRect{settingsX, 52.0f, settingsWidth, 58.0f};
  return SDL_FRect{settingsX - 16.0f - libraryWidth, 52.0f, libraryWidth,
                   58.0f};
}

// The shelf shows seven whole cards plus half of the next one, so the cut-off
// card at the right edge is always the cue that the row can travel. A library
// that fits leaves an empty slot there instead of pretending there is more.
constexpr float kVisibleSlots = 7.5f;

float slotPitch() { return (kRowRight - kRowLeft) / kVisibleSlots; }

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
              bool focused, float focusOpacity,
              const library::GameEntry *entry) {
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

  if (entry == nullptr) {
    rounded(renderer, x, y, width, height, radius, {13, 20, 36, 255});
    rounded(renderer, x + 1, y + 1, width - 2, height - 2, radius - 1,
            {19, 28, 48, 255});
    if (focused)
      drawFocusRing(renderer, x, y, width, height, radius, focusOpacity);
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
  // Selection fills the card and scales it up; the ring says the direction keys
  // are on it, so it fades away when they move to a pill or a footer action.
  if (focused)
    drawFocusRing(renderer, x, y, width, height, radius, focusOpacity);
}

// Footer geometry lives here because the drawing and the hit test have to agree
// on it; the metrics themselves come from the hint helpers.
constexpr float kFooterRight = 1528.0f;
constexpr float kFooterY = 822.0f;

// The quit confirmation is centred on the logical canvas so it covers the
// shelf no matter which part of it has focus.
constexpr float kQuitDialogWidth = 760.0f;
constexpr float kQuitDialogHeight = 290.0f;
constexpr float kQuitDialogX = (1600.0f - kQuitDialogWidth) * 0.5f;
constexpr float kQuitDialogY = 300.0f;
constexpr float kQuitButtonWidth = 220.0f;
constexpr float kQuitButtonHeight = 64.0f;
constexpr float kQuitButtonY = 452.0f;
constexpr float kQuitCancelX = 520.0f;
constexpr float kQuitConfirmX = 860.0f;

// Only the two actions live in the footer; leaving the launcher is done by
// closing the window, the way a console has no "quit" entry on its home row.
std::vector<Hint> footerHints(bool hasGame, i18n::Language language,
                              const config::InputPrompts &prompts) {
  std::vector<Hint> hints;
  if (hasGame) {
    hints.push_back(
        {i18n::tr(i18n::TextId::Start, language), prompts.confirm, true});
    hints.push_back({i18n::tr(i18n::TextId::ManageGame, language),
                     prompts.manage, false});
  }
  return hints;
}

} // namespace

void drawHome(SDL_Renderer *renderer, int selectedGame, float time,
              Uint64 toastUntil, std::string_view toastMessage, int startupPage,
              i18n::Language language, const library::GameLibrary &gameLibrary,
              const config::InputPrompts &prompts, HomeFocus focus,
              int focusedPill, int focusedAction, float rowOffset,
              float focusOpacity) {
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
  const std::size_t libraryCount = gameLibrary.entries().size();
  const SDL_FRect libraryPill = pillRect(false, language, libraryCount);
  const SDL_FRect settingsPill = pillRect(true, language, libraryCount);
  rounded(renderer, libraryPill.x, libraryPill.y, libraryPill.w,
          libraryPill.h, 29, {4, 10, 26, 145});
  circle(renderer, libraryPill.x + 33, 81, 15, style.accent);
  text(renderer, static_cast<int>(libraryPill.x) + 26, 66, "+", 2,
       {18, 18, 33, 255});
  text(renderer, static_cast<int>(libraryPill.x) + 60, 66,
       tr(TextId::Library, language), 2, {255, 255, 255, 255});
  const std::string countLabel = std::to_string(libraryCount);
  text(renderer,
       static_cast<int>(libraryPill.x + libraryPill.w - 36.0f -
                        static_cast<float>(textWidth(countLabel, 1))),
       71, countLabel, 1, {191, 201, 220, 255});
  rounded(renderer, settingsPill.x, settingsPill.y, settingsPill.w,
          settingsPill.h, 29, {4, 10, 26, 175});
  circle(renderer, settingsPill.x + 33, 81, 15, {255, 255, 255, 45});
  text(renderer, static_cast<int>(settingsPill.x) + 27, 66, "?", 2,
       style.accent);
  text(renderer, static_cast<int>(settingsPill.x) + 59, 66,
       tr(TextId::Settings, language), 2, {255, 255, 255, 255});
  // The pills are buttons too, so they take the same ring when the direction
  // keys are on them. The ring is drawn over the pills rather than under them,
  // because they are translucent and would tint it.
  if (focus == HomeFocus::Pills) {
    const SDL_FRect pill = focusedPill == 0 ? libraryPill : settingsPill;
    drawFocusRing(renderer, pill.x, pill.y, pill.w, pill.h, 29.0f,
                  focusOpacity);
  }

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

  const bool cardsFocused = focus == HomeFocus::Cards;
  // The shelf is a window, not a canvas: a card that sticks out past the row is
  // cut there, which is what turns the eighth card into the half card that
  // shows there is more to the right.
  SDL_Rect shelfClip{static_cast<int>(kRowLeft), 380,
                     static_cast<int>(kRowRight - kRowLeft), 320};
  SDL_RenderSetClipRect(renderer, &shelfClip);
  // Draw one column beyond each edge so a card sliding out and the next one
  // sliding in both stay whole while the row travels.
  const int firstColumn = static_cast<int>(std::floor(rowOffset));
  // Always fill the window, including the half column at the right edge: an
  // empty shelf still shows half a placeholder there, so the row reads as a
  // shelf that continues rather than one that simply stops.
  const int lastColumn = firstColumn + static_cast<int>(std::ceil(kVisibleSlots));
  for (int i = std::max(0, firstColumn - 1); i <= lastColumn; ++i) {
    if (i == selectedGame)
      continue;
    const library::GameEntry *entry =
        i < static_cast<int>(gameLibrary.entries().size())
            ? &gameLibrary.entries()[static_cast<std::size_t>(i)]
            : nullptr;
    drawCard(renderer, i, static_cast<float>(i) - rowOffset, false, false,
             focusOpacity, entry);
  }
  drawCard(renderer, selectedGame,
           static_cast<float>(selectedGame) - rowOffset, true, cardsFocused,
           focusOpacity, selectedEntry);
  SDL_RenderSetClipRect(renderer, nullptr);

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
  const std::vector<Hint> footer =
      footerHints(selectedEntry != nullptr, language, prompts);
  const int focused =
      focus == HomeFocus::Actions && !footer.empty()
          ? std::clamp(focusedAction, 0, static_cast<int>(footer.size()) - 1)
          : -1;
  float cursor =
      kFooterRight - static_cast<float>(hintRowWidth(footer));
  for (int index = 0; index < static_cast<int>(footer.size()); ++index) {
    if (index == focused)
      drawHintFocus(renderer, cursor, kFooterY, footer[index], focusOpacity);
    drawHint(renderer, cursor, kFooterY, footer[index], style.accent);
    cursor += static_cast<float>(hintWidth(footer[index])) + kHintGap;
  }

  if (SDL_GetTicks64() < toastUntil && !toastMessage.empty()) {
    rounded(renderer, 484, 760, 632, 66, 18, {4, 8, 22, 235});
    centeredText(renderer, 800, 779, std::string(toastMessage), 2,
                 style.accent);
  }
}

// Nothing to act on when the library is empty, so the footer is empty too.
int homeActionCount(bool hasGame) { return hasGame ? 2 : 0; }

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

int homePillAt(float x, float y, i18n::Language language, std::size_t count) {
  if (y < 49.0f || y > 113.0f)
    return -1;
  const SDL_FRect libraryPill = pillRect(false, language, count);
  if (x >= libraryPill.x && x <= libraryPill.x + libraryPill.w)
    return 0;
  const SDL_FRect settingsPill = pillRect(true, language, count);
  if (x >= settingsPill.x && x <= settingsPill.x + settingsPill.w)
    return 1;
  return -1;
}

int homeCardAt(float x, float y, int count, float rowOffset) {
  // The selected card is drawn a little larger, so accept either size and a
  // click on its rim still counts.
  const int firstColumn = static_cast<int>(std::floor(rowOffset));
  const int lastSlot =
      std::min(count - 1, firstColumn + static_cast<int>(kVisibleSlots) + 1);
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
  // The row stops at the last game instead of wrapping, so the right edge is
  // flush with the seventh slot once the shelf reaches its end.
  const float overflow =
      std::max(0.0f, static_cast<float>(count) - kVisibleSlots);
  const float start =
      std::clamp(static_cast<float>(selectedGame) - (kVisibleSlots - 1.5f),
                 0.0f, overflow);
  return start;
}

void drawQuitConfirm(SDL_Renderer *renderer, int choice,
                     i18n::Language language,
                     const config::InputPrompts &prompts, SDL_Color accent) {
  using i18n::TextId;
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {2, 5, 12, 160});
  rounded(renderer, kQuitDialogX + 8, kQuitDialogY + 10, kQuitDialogWidth,
          kQuitDialogHeight, 28, {0, 0, 0, 70});
  rounded(renderer, kQuitDialogX, kQuitDialogY, kQuitDialogWidth,
          kQuitDialogHeight, 28, {12, 21, 37, 245});
  rounded(renderer, kQuitDialogX + 36, kQuitDialogY + 40, 4, 32, 2, accent);
  text(renderer, kQuitDialogX + 56, kQuitDialogY + 42,
       tr(TextId::QuitConfirmTitle, language), 2, accent);
  text(renderer, kQuitDialogX + 36, kQuitDialogY + 106,
       tr(TextId::QuitConfirmBody, language), 2, {206, 216, 232, 255});

  const std::array<TextId, 2> labels{TextId::Cancel, TextId::Quit};
  const std::array<float, 2> xs{kQuitCancelX, kQuitConfirmX};
  for (int option = 0; option < 2; ++option) {
    const bool selected = option == choice;
    rounded(renderer, xs[static_cast<std::size_t>(option)], kQuitButtonY,
            kQuitButtonWidth, kQuitButtonHeight, 18,
            selected ? accent : SDL_Color{255, 255, 255, 18});
    centeredText(renderer,
                 static_cast<int>(xs[static_cast<std::size_t>(option)] +
                                  kQuitButtonWidth / 2.0f),
                 static_cast<int>(kQuitButtonY + 22),
                 tr(labels[static_cast<std::size_t>(option)], language), 2,
                 selected ? SDL_Color{24, 24, 39, 255}
                          : SDL_Color{230, 235, 245, 255});
    if (selected) {
      drawFocusRing(renderer, xs[static_cast<std::size_t>(option)],
                    kQuitButtonY, kQuitButtonWidth, kQuitButtonHeight, 18.0f,
                    1.0f);
    }
  }
  const std::vector<Hint> footer{
      {tr(TextId::Cancel, language), prompts.back, false},
      {tr(TextId::ConfirmControl, language), prompts.confirm, true}};
  drawHintRow(renderer, kQuitDialogX + kQuitDialogWidth - 36.0f,
              kQuitDialogY + kQuitDialogHeight - 46.0f, footer, accent);
}

int quitConfirmChoiceAt(float x, float y) {
  if (y < kQuitButtonY || y > kQuitButtonY + kQuitButtonHeight)
    return -1;
  if (x >= kQuitCancelX && x <= kQuitCancelX + kQuitButtonWidth)
    return 0;
  if (x >= kQuitConfirmX && x <= kQuitConfirmX + kQuitButtonWidth)
    return 1;
  return -1;
}

void moveSelection(int &selectedGame, int direction, int count) {
  if (count <= 0)
    return;
  selectedGame = (selectedGame + direction + count) % count;
}

} // namespace launcher::ui
