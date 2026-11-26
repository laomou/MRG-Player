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

// One accent for every screen, so the cards step back and let the box art say
// which console a game came from; only the corner badge wears the accent.
constexpr SDL_Color kCardTop{31, 42, 68, 255};
constexpr SDL_Color kCardBottom{9, 17, 36, 255};

// Two or three letters, so the badge fits the narrowest card.
const char *platformBadge(game::GamePlatform platform) {
  switch (platform) {
  case game::GamePlatform::Nes:
    return "NES";
  case game::GamePlatform::Gba:
    return "GBA";
  case game::GamePlatform::Snes:
    return "SNES";
  case game::GamePlatform::GameBoy:
    return "GB";
  case game::GamePlatform::Genesis:
    return "MD";
  case game::GamePlatform::Unknown:
    break;
  }
  return "?";
}

// The row never moves: every slot keeps its place and only the highlight
// travels, so the first game always sits against the left edge.
constexpr float kRowLeft = 58.0f;
constexpr float kRowRight = 1222.0f;
constexpr float kRowBottom = 542.0f;
constexpr float kCardAspect = 314.0f / 226.0f;
constexpr float kCardGap = 16.0f;
constexpr float kMaximumCardWidth = 181.0f;

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
      std::max(148.0f, 36.0f + static_cast<float>(settingsLabel) + 16.0f);
  const float libraryWidth =
      std::max(220.0f, 36.0f + static_cast<float>(libraryLabel) + 13.0f +
               static_cast<float>(countLabel) + 16.0f);
  const float settingsX = kRowRight - settingsWidth;
  if (settings)
    return SDL_FRect{settingsX, 42.0f, settingsWidth, 46.0f};
  return SDL_FRect{settingsX - 13.0f - libraryWidth, 42.0f, libraryWidth,
                   46.0f};
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

// A title over box art needs contrast, but a solid plate reads as a black bar.
// Fading the card's bottom instead keeps the art readable and the corners stay
// round, because every row is inset by the radius the card already uses.
void drawTitleScrim(SDL_Renderer *renderer, float x, float y, float width,
                    float height, float radius, Uint8 strength) {
  fadeBottomRounded(renderer, x, y + height * 0.50f, width, height * 0.50f,
                    radius, {4, 6, 16, 255}, strength, 1.6f);
}

void drawCard(SDL_Renderer *renderer, float column, bool selected,
              bool focused, float focusOpacity, SDL_Color accent,
              const library::GameEntry *entry, CoverSource *covers) {
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

  SDL_Texture *art =
      covers != nullptr ? covers->cover(renderer, *entry) : nullptr;
  if (art != nullptr) {
    // The art already carries rounded corners, so it can sit under the title
    // plate untouched; the plate hides whatever the crop would show there.
    SDL_SetTextureAlphaMod(art, alpha);
    drawTexture(renderer, art, x, y, width, height);
    SDL_SetTextureAlphaMod(art, 255);
  } else {
    gradientRounded(renderer, x, y, width, height, radius,
                    {kCardTop.r, kCardTop.g, kCardTop.b, alpha},
                    {kCardBottom.r, kCardBottom.g, kCardBottom.b, alpha});
  }

  const float margin = 12.0f * unit;
  // The console badge owns the top-right corner, so the favourite marker moves
  // to the left where it never fights with it.
  if (entry->favorite) {
    const float badge = 28.0f * unit;
    circle(renderer, static_cast<int>(x + badge), static_cast<int>(y + badge),
           16.0f * unit, {255, 220, 114, static_cast<Uint8>(alpha * 0.9f)});
    centeredText(renderer, static_cast<int>(x + badge),
                 static_cast<int>(y + badge - 7.0f * unit), "*", 2,
                 {18, 18, 33, alpha});
  }

  // Box art is busy and a solid plate reads as a black bar across the bottom of
  // the card, so the title sits on a fade that follows the rounded corners and
  // keeps the art visible underneath.
  drawTitleScrim(renderer, x, y, width, height, radius,
                 art != nullptr ? 246 : static_cast<Uint8>(alpha * 0.85f));
  const int scale = selected && width >= 240.0f ? 3 : 2;
  // The dimmed cards keep a brighter title than the rest of the card so the
  // name stays readable over the art.
  const Uint8 titleAlpha = selected ? 255 : 220;
  centeredGlyph(renderer, static_cast<int>(x + width * 0.5f),
                static_cast<int>(y + height - margin - 14.0f),
                fitText(entry->title, static_cast<int>(width - margin * 2.0f),
                         scale),
                scale, {255, 255, 255, titleAlpha});

  // The console badge: a small mark in the corner instead of a second line of
  // text, so the card says what it is without shouting it.
  const char *badgeLabel = platformBadge(entry->platform);
  const float badgeHeight = 20.0f * unit;
  const float badgeWidth =
      std::max(badgeHeight,
               static_cast<float>(textWidth(badgeLabel, 1)) + 10.0f * unit);
  const float badgeX = x + width - margin - badgeWidth;
  const float badgeY = y + margin;
  rounded(renderer, badgeX, badgeY, badgeWidth, badgeHeight,
          badgeHeight * 0.5f,
          {accent.r, accent.g, accent.b, static_cast<Uint8>(alpha * 0.85f)});
  centeredGlyph(renderer, static_cast<int>(badgeX + badgeWidth * 0.5f),
                static_cast<int>(badgeY + badgeHeight * 0.5f), badgeLabel, 1,
                {10, 15, 28, alpha});
  // Selection fills the card and scales it up; the ring says the direction keys
  // are on it, so it fades away when they move to a pill or a footer action.
  if (focused)
    drawFocusRing(renderer, x, y, width, height, radius, focusOpacity);
}

// Footer geometry lives here because the drawing and the hit test have to agree
// on it; the metrics themselves come from the hint helpers.
constexpr float kFooterRight = 1222.0f;
constexpr float kFooterY = 658.0f;

// The quit confirmation is centred on the logical canvas so it covers the
// shelf no matter which part of it has focus.
const ConfirmLayout kQuitLayout =
    confirmLayout({336, 240, 608, 232}, 122, 80, 96, 16);

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
              float focusOpacity, SDL_Color accent, CoverSource *covers,
              bool showFooter) {
  using i18n::TextId;
  using i18n::tr;
  const int slotCount = static_cast<int>(gameLibrary.slotCount());
  selectedGame = std::clamp(selectedGame, 0, slotCount - 1);
  const bool libraryEmpty = gameLibrary.entries().empty();
  const library::GameEntry *selectedEntry =
      selectedGame < static_cast<int>(gameLibrary.entries().size())
          ? &gameLibrary.entries()[static_cast<std::size_t>(selectedGame)]
          : nullptr;
  // The background is the launcher's own, not the selected game's: one accent
  // paints every screen so the chrome never changes colour with the library.
  Game style{};
  style.top = kCardTop;
  style.bottom = kCardBottom;
  style.accent = accent;

  SDL_Texture *wallpaper =
      covers != nullptr && selectedEntry != nullptr
          ? covers->wallpaper(renderer, *selectedEntry)
          : nullptr;
  drawBackground(renderer, style, time, wallpaper);
  rounded(renderer, 58, 46, 4, 26, 2, style.accent);
  text(renderer, 74, 43, "MRG PLAYER", 3, {255, 255, 255, 255});
  text(renderer, 74, 79, "RETRO GAMES. TIMELESS PLAY.", 1,
       {167, 184, 207, 255});
  const std::size_t libraryCount = gameLibrary.entries().size();
  const SDL_FRect libraryPill = pillRect(false, language, libraryCount);
  const SDL_FRect settingsPill = pillRect(true, language, libraryCount);
  rounded(renderer, libraryPill.x, libraryPill.y, libraryPill.w,
          libraryPill.h, 23, {4, 10, 26, 145});
  circle(renderer, libraryPill.x + 26, 65, 12, style.accent);
  text(renderer, static_cast<int>(libraryPill.x) + 19, 50, "+", 2,
       {18, 18, 33, 255});
  text(renderer, static_cast<int>(libraryPill.x) + 48, 50,
       tr(TextId::Library, language), 2, {255, 255, 255, 255});
  const std::string countLabel = std::to_string(libraryCount);
  text(renderer,
       static_cast<int>(libraryPill.x + libraryPill.w - 29.0f -
                        static_cast<float>(textWidth(countLabel, 1))),
        57, countLabel, 1, {191, 201, 220, 255});
  rounded(renderer, settingsPill.x, settingsPill.y, settingsPill.w,
          settingsPill.h, 23, {4, 10, 26, 175});
  circle(renderer, settingsPill.x + 26, 65, 12, {255, 255, 255, 45});
  gearIcon(renderer, settingsPill.x + 26.0f, 65.0f, 10.0f, style.accent);
  text(renderer, static_cast<int>(settingsPill.x) + 47, 50,
       tr(TextId::Settings, language), 2, {255, 255, 255, 255});
  // The pills are buttons too, so they take the same ring when the direction
  // keys are on them. The ring is drawn over the pills rather than under them,
  // because they are translucent and would tint it.
  if (focus == HomeFocus::Pills) {
    const SDL_FRect pill = focusedPill == 0 ? libraryPill : settingsPill;
    drawFocusRing(renderer, pill.x, pill.y, pill.w, pill.h, 23.0f,
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
  text(renderer, 58, 128, tr(sectionTitle, language), 1, style.accent);
  text(renderer, 58, 154, tr(TextId::ChooseYourGame, language), 3,
       {255, 255, 255, 255});
  text(renderer, 58, 190, tr(sectionSubtitle, language), 1,
       {191, 201, 220, 255});

  const bool cardsFocused = focus == HomeFocus::Cards;
  // The shelf is a window, not a canvas: a card that sticks out past the row is
  // cut there, which is what turns the eighth card into the half card that
  // shows there is more to the right.
  setClipRect(renderer, kRowLeft, 304.0f, kRowRight - kRowLeft, 256.0f);
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
    drawCard(renderer, static_cast<float>(i) - rowOffset, false, false,
             focusOpacity, accent, entry, covers);
  }
  drawCard(renderer, static_cast<float>(selectedGame) - rowOffset, true,
           cardsFocused, focusOpacity, accent, selectedEntry, covers);
  clearClipRect(renderer);

  if (selectedEntry != nullptr) {
    centeredText(renderer, 640, 569, fitText(selectedEntry->title, 608, 3), 3,
                 style.accent);
    const std::uint64_t hours = selectedEntry->playSeconds / 3600;
    const std::uint64_t minutes = (selectedEntry->playSeconds % 3600) / 60;
    std::string playTime = tr(TextId::PlayTime, language);
    playTime += ": ";
    if (hours > 0)
      playTime += std::to_string(hours) + "h ";
    playTime += std::to_string(minutes) + "m";
    centeredText(renderer, 640, 606, playTime, 1, {232, 236, 244, 255});
  }

  // The shelf is the only screen with a footer of its own; the management
  // drawer covers it, so the caller can turn it off and let the drawer draw
  // its own legend in the space it actually owns.
  if (showFooter) {
    fill(renderer, 58, 638, 1164, 1, {255, 255, 255, 24});
    const std::vector<Hint> footer =
        footerHints(selectedEntry != nullptr, language, prompts);
    const int focused =
        focus == HomeFocus::Actions && !footer.empty()
            ? std::clamp(focusedAction, 0, static_cast<int>(footer.size()) - 1)
            : -1;
    drawHintRow(renderer, kFooterRight, kFooterY, footer, style.accent,
                focused, focusOpacity);
  }

  if (SDL_GetTicks64() < toastUntil && !toastMessage.empty()) {
    rounded(renderer, 387, 608, 506, 53, 14, {4, 8, 22, 235});
    centeredText(renderer, 640, 623, std::string(toastMessage), 2,
                 style.accent);
  }
}

// Nothing to act on when the library is empty, so the footer is empty too.
int homeActionCount(bool hasGame) { return hasGame ? 2 : 0; }

int homeActionAt(float x, float y, bool hasGame, i18n::Language language,
                 const config::InputPrompts &prompts) {
  const auto layout = hintLayout(kFooterRight, kFooterY,
                                 footerHints(hasGame, language, prompts));
  for (std::size_t index = 0; index < layout.size(); ++index) {
    // Retain generous pointer padding around the small footer legend.
    if (layout[index].expanded(10, 13).translated(0, 1).contains(x, y))
      return static_cast<int>(index);
  }
  return -1;
}

int homePillAt(float x, float y, i18n::Language language, std::size_t count) {
  if (y < 39.0f || y > 91.0f)
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
  drawConfirmFrame(renderer, kQuitLayout, tr(TextId::QuitConfirmTitle, language),
                   tr(TextId::QuitConfirmBody, language), accent, 160);
  drawChoiceButtons(renderer, kQuitLayout.buttons,
                    {tr(TextId::Cancel, language), tr(TextId::Quit, language)},
                    choice, accent);
  const std::vector<Hint> footer{
      {tr(TextId::Cancel, language), prompts.back, false},
      {tr(TextId::ConfirmControl, language), prompts.confirm, true}};
  drawHintRow(renderer, kQuitLayout.footer.right(), kQuitLayout.footer.y,
              footer, accent);
}

int quitConfirmChoiceAt(float x, float y) {
  return kQuitLayout.buttons.hit(x, y);
}

void moveSelection(int &selectedGame, int direction, int count) {
  if (count <= 0)
    return;
  selectedGame = (selectedGame + direction + count) % count;
}

} // namespace launcher::ui
