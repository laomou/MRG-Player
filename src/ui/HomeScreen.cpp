#include "ui/HomeScreen.hpp"

#include <algorithm>
#include <cctype>
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
    return "FC";
  case game::GamePlatform::Gba:
    return "GBA";
  case game::GamePlatform::Snes:
    return "SFC";
  case game::GamePlatform::GameBoy:
    return "GB";
  case game::GamePlatform::Genesis:
    return "MD";
  case game::GamePlatform::Unknown:
    break;
  }
  return "?";
}

constexpr float kCardAspect = 314.0f / 226.0f;
constexpr float kCardGap = 16.0f;
constexpr float kMaximumCardWidth = 181.0f;
// Seven whole cards plus half of the next at 1280 wide. A narrower canvas keeps
// the cards at their pixel size and shows fewer of them instead: shrinking the
// row to fit would shrink the type on the cards with it.
constexpr float kVisibleSlotsAt1280 = 7.5f;

// The shelf reads its margins from the canvas edges and its footer stack from
// the bottom edge. The same numbers copied into a 4:3 canvas would only fit by
// scaling the whole screen by 0.8, which is what turned the interface blurry on
// the handheld; letting the shelf absorb the difference keeps every pixel and
// every glyph at its natural size.
struct HomeLayout {
  float marginX = 0.0f;
  float centerX = 0.0f;
  float headerY = 0.0f;      // the two header pills
  float headerHeight = 0.0f;
  float brandX = 0.0f;
  float brandY = 0.0f;
  float brandTitleY = 0.0f;
  float brandSubtitleY = 0.0f;
  float sectionLabelY = 0.0f;
  float sectionTitleY = 0.0f;
  float sectionSubtitleY = 0.0f;
  float shelfTop = 0.0f;     // where a travelling card is cut off
  float shelfBottom = 0.0f;  // every card rests on this line
  float rowLeft = 0.0f;
  float rowRight = 0.0f;
  float captionY = 0.0f;     // the selected game's title
  float captionWidth = 0.0f;
  float playTimeY = 0.0f;
  float dividerY = 0.0f;
  float footerY = 0.0f;
  float footerRight = 0.0f;
  float toastX = 0.0f;
  float toastWidth = 0.0f;
  float visibleSlots = kVisibleSlotsAt1280;
};

HomeLayout homeLayout() {
  HomeLayout layout;
  const float width = static_cast<float>(kCanvasWidth);
  const float height = static_cast<float>(kCanvasHeight);
  layout.centerX = width * 0.5f;
  // 58 of 1280: the margin grows with the canvas instead of being copied.
  layout.marginX = std::round(width * (58.0f / 1280.0f));
  layout.brandX = layout.marginX;
  layout.brandY = 46.0f;
  layout.brandTitleY = 43.0f;
  layout.brandSubtitleY = 79.0f;
  layout.headerY = 42.0f;
  layout.headerHeight = 46.0f;
  layout.sectionLabelY = 128.0f;
  layout.sectionTitleY = 154.0f;
  layout.sectionSubtitleY = 190.0f;
  layout.shelfTop = layout.sectionSubtitleY + 114.0f;
  layout.rowLeft = layout.marginX;
  layout.rowRight = width - layout.marginX;
  layout.footerRight = layout.rowRight;
  // The footer stack is measured up from the bottom edge, so a taller canvas
  // hands its extra height to the shelf rather than to the gap under the hints.
  layout.footerY = height - 62.0f;
  layout.dividerY = height - 82.0f;
  layout.playTimeY = height - 114.0f;
  layout.captionY = height - 151.0f;
  layout.shelfBottom = height - 178.0f;
  layout.captionWidth =
      std::min(608.0f, layout.rowRight - layout.rowLeft);
  layout.toastWidth = std::min(506.0f, layout.rowRight - layout.rowLeft);
  layout.toastX = layout.centerX - layout.toastWidth * 0.5f;
  // Keep the half card that says the row continues: the count scales with the
  // width, but a whole number of slots would hide the next card completely on a
  // narrower canvas, so it always ends in a half.
  layout.visibleSlots =
      std::max(2.5f, std::floor(kVisibleSlotsAt1280 * width / 1280.0f) + 0.5f);
  return layout;
}

struct CardBox {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// The two header pills are sized from their labels and anchored against the
// right margin, so a longer translation never runs past the edge of the
// canvas; with the current Chinese labels the pair keeps its old geometry.
SDL_FRect pillRect(const HomeLayout &layout, bool settings,
                    i18n::Language language, std::size_t count) {
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
  const float settingsX = layout.rowRight - settingsWidth;
  if (settings)
    return SDL_FRect{settingsX, layout.headerY, settingsWidth,
                     layout.headerHeight};
  return SDL_FRect{settingsX - 13.0f - libraryWidth, layout.headerY,
                   libraryWidth, layout.headerHeight};
}

// The shelf shows whole cards plus half of the next one, so the cut-off card at
// the right edge is always the cue that the row can travel. A library that fits
// leaves an empty slot there instead of pretending there is more.
float slotPitch(const HomeLayout &layout) {
  return (layout.rowRight - layout.rowLeft) / layout.visibleSlots;
}

CardBox cardBox(const HomeLayout &layout, float column, bool selected) {
  const float base =
      std::min(kMaximumCardWidth, slotPitch(layout) - kCardGap);
  const float scale = selected ? 1.04f : 1.0f;
  const float width = base * scale;
  const float height = base * kCardAspect * scale;
  const float centre = layout.rowLeft + slotPitch(layout) * (column + 0.5f);
  return {centre - width * 0.5f, layout.shelfBottom - height, width, height};
}

// A title over box art needs contrast, but a solid plate reads as a black bar.
// Fading the card's bottom instead keeps the art readable and the corners stay
// round, because every row is inset by the radius the card already uses.
void drawTitleScrim(SDL_Renderer *renderer, float x, float y, float width,
                    float height, float radius, Uint8 strength) {
  drawCardScrim(renderer, x, y + height * 0.50f, width, height * 0.50f,
                radius, {4, 6, 16, 255}, strength, 1.6f);
}

void drawCard(SDL_Renderer *renderer, const HomeLayout &layout, float column,
              bool selected, bool focused, float focusOpacity, SDL_Color accent,
              const library::GameEntry *entry, CoverSource *covers) {
  const CardBox box = cardBox(layout, column, selected);
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
    drawCardSlot(renderer, x, y, width, height, radius, {13, 20, 36, 255},
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
    drawCardPanel(renderer, x, y, width, height, radius, kCardTop, kCardBottom,
                  alpha);
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

// The tile at the end of the shelf is not a card: a ring and four squares say
// "there is more" without pretending to be a game. There is no outline
// primitive, so the ring is stroked in short radial segments; at this size the
// joins are invisible.
void drawRing(SDL_Renderer *renderer, float centreX, float centreY, float radius,
              float thickness, SDL_Color color) {
  if (renderer == nullptr || radius <= 0.0f)
    return;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  // Enough segments that the stroke reads as a ring instead of a dotted line;
  // the lines are drawn radially and this keeps their ends overlapping.
  const int steps = std::max(180, static_cast<int>(radius * 8.0f));
  const float inner = radius - thickness * 0.5f;
  const float outer = radius + thickness * 0.5f;
  for (int i = 0; i < steps; ++i) {
    const float angle = 6.2831853f * static_cast<float>(i) /
                        static_cast<float>(steps);
    const float cosAngle = std::cos(angle);
    const float sinAngle = std::sin(angle);
    SDL_RenderDrawLine(
        renderer, static_cast<int>(std::lround(centreX + cosAngle * inner)),
        static_cast<int>(std::lround(centreY + sinAngle * inner)),
        static_cast<int>(std::lround(centreX + cosAngle * outer)),
        static_cast<int>(std::lround(centreY + sinAngle * outer)));
  }
}

void drawMoreTile(SDL_Renderer *renderer, const HomeLayout &layout,
                  float column, bool selected, bool focused, float focusOpacity,
                  SDL_Color accent, i18n::Language language) {
  const CardBox box = cardBox(layout, column, false);
  const float radius = box.width * 0.40f;
  const float centreX = box.x + box.width * 0.5f;
  // The tile is a disc, not a card: it sits on the same centre line as the
  // covers either side of it, so the ring reads as part of the row.
  const float centreY = layout.shelfBottom - box.height * 0.5f;
  const std::string label =
      i18n::tr(i18n::TextId::MoreGames, language);
  centeredText(renderer, static_cast<int>(centreX),
               static_cast<int>(centreY - radius - 34.0f), label, 2,
               {255, 255, 255, 255});
  // The ring is the tile's body: a lighter stroke when it is merely on screen,
  // the full accent when it holds the selection.
  const SDL_Color ringColor{accent.r, accent.g, accent.b,
                            static_cast<Uint8>(selected ? 255 : 200)};
  drawRing(renderer, centreX, centreY, radius, 4.0f, ringColor);
  const float square = radius * 0.42f;
  const float gap = radius * 0.14f;
  const float offset = (square + gap) * 0.5f;
  const float corner = square * 0.22f;
  for (int i = 0; i < 4; ++i) {
    const float x = centreX + ((i % 2) == 0 ? -offset : offset) - square * 0.5f;
    const float y = centreY + (i < 2 ? -offset : offset) - square * 0.5f;
    rounded(renderer, x, y, square, square, corner, accent);
  }
  if (focused) {
    // A second, whiter ring outside the accent one, so the tile reads as chosen
    // even when the accent is close to the background.
    drawRing(renderer, centreX, centreY, radius + 6.0f, 2.0f,
             {255, 255, 255, static_cast<Uint8>(235.0f * focusOpacity)});
  }
}

// The quit confirmation is centred on the canvas so it covers the shelf no
// matter which part of it has focus. It narrows with the canvas instead of
// running off it, and the buttons follow from the same bounds. The body gets a
// real box of its own: the buttons start low enough that the sentence never
// runs into them.
ConfirmLayout quitLayout() {
  const float width =
      std::min(608.0f, static_cast<float>(kCanvasWidth) - 96.0f);
  // Just the heading, one line of body text and the two buttons: the dialog no
  // longer carries a key legend underneath, so the box ends below the buttons.
  constexpr float height = 240.0f;
  return confirmLayout({(static_cast<float>(kCanvasWidth) - width) * 0.5f,
                        (static_cast<float>(kCanvasHeight) - height) * 0.5f,
                        width, height},
                       158, 80, 96, 16);
}

// The keys the shelf answers to, written as a legend rather than a row of
// buttons: the cards stay the only thing the direction keys act on, so the
// strip never takes focus and the pointer never has to aim at a word. One word
// per key, because the shelf above already says which game it is.
std::vector<Hint> footerHints(bool hasGame, bool moreSelected,
                              i18n::Language language,
                              const config::InputPrompts &prompts) {
  std::vector<Hint> hints;
  if (moreSelected) {
    // The tile opens the cover wall: there is nothing to manage yet, so it
    // only names the one button that does something on it.
    hints.push_back(
        {i18n::tr(i18n::TextId::ConfirmControl, language), prompts.confirm,
         true});
  } else if (hasGame) {
    // Confirming is the primary action, so it sits on the right where every
    // other screen puts it; the drawer is secondary and takes the left slot.
    hints.push_back({i18n::tr(i18n::TextId::HintOptions, language),
                     prompts.options, false});
    hints.push_back(
        {i18n::tr(i18n::TextId::ConfirmControl, language), prompts.confirm,
         true});
  }
  return hints;
}

} // namespace

void drawHome(SDL_Renderer *renderer, int selectedGame, Uint64 toastUntil, std::string_view toastMessage, int startupPage,
              i18n::Language language, const library::GameLibrary &gameLibrary,
              const config::InputPrompts &prompts, HomeFocus focus,
              int focusedPill, float rowOffset,
              float focusOpacity, SDL_Color accent, CoverSource *covers,
              bool showFooter, bool showPills) {
  using i18n::TextId;
  using i18n::tr;
  // Ten game cards, then the tile that opens the cover wall. The selection may
  // rest on any of them: an empty card says nothing and draws no footer, and
  // the tile only offers the one action it has.
  const int gameSlots = static_cast<int>(gameLibrary.slotCount());
  const int moreSlot = gameSlots;
  selectedGame = std::clamp(selectedGame, 0, moreSlot);
  const bool libraryEmpty = gameLibrary.entries().empty();
  const bool moreSelected = selectedGame == moreSlot;
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
  const HomeLayout layout = homeLayout();
  drawBackground(renderer, style, wallpaper);
  rounded(renderer, layout.brandX, layout.brandY, 4, 26, 2, style.accent);
  text(renderer, static_cast<int>(layout.brandX) + 16,
       static_cast<int>(layout.brandTitleY), "MRG PLAYER", 3,
       {255, 255, 255, 255});
  text(renderer, static_cast<int>(layout.brandX) + 16,
       static_cast<int>(layout.brandSubtitleY), "RETRO GAMES. TIMELESS PLAY.",
       1, {167, 184, 207, 255});
  const std::size_t libraryCount = gameLibrary.entries().size();
  const SDL_FRect libraryPill = pillRect(layout, false, language, libraryCount);
  const SDL_FRect settingsPill = pillRect(layout, true, language, libraryCount);
  const int pillCenterY =
      static_cast<int>(layout.headerY + layout.headerHeight * 0.5f);
  const int pillLabelY = static_cast<int>(layout.headerY) + 8;
  const int pillCountY = static_cast<int>(layout.headerY) + 15;
  if (showPills) {
    if (libraryPillVisible()) {
      rounded(renderer, libraryPill.x, libraryPill.y, libraryPill.w,
              libraryPill.h, 23, {4, 10, 26, 145});
      circle(renderer, libraryPill.x + 26, pillCenterY, 12, style.accent);
      text(renderer, static_cast<int>(libraryPill.x) + 19, pillLabelY, "+", 2,
           {18, 18, 33, 255});
      text(renderer, static_cast<int>(libraryPill.x) + 48, pillLabelY,
           tr(TextId::Library, language), 2, {255, 255, 255, 255});
      const std::string countLabel = std::to_string(libraryCount);
      text(renderer,
           static_cast<int>(libraryPill.x + libraryPill.w - 29.0f -
                            static_cast<float>(textWidth(countLabel, 1))),
           pillCountY, countLabel, 1, {191, 201, 220, 255});
    }
    rounded(renderer, settingsPill.x, settingsPill.y, settingsPill.w,
            settingsPill.h, 23, {4, 10, 26, 175});
    circle(renderer, settingsPill.x + 26, pillCenterY, 12, {255, 255, 255, 45});
    gearIcon(renderer, settingsPill.x + 26.0f,
             static_cast<float>(pillCenterY), 10.0f, style.accent);
    text(renderer, static_cast<int>(settingsPill.x) + 47, pillLabelY,
         tr(TextId::Settings, language), 2, {255, 255, 255, 255});
    // The pills are buttons too, so they take the same ring when the direction
    // keys are on them. The ring is drawn over the pills rather than under them,
    // because they are translucent and would tint it.
    if (focus == HomeFocus::Pills) {
      const SDL_FRect pill =
          focusedPill == 0 && libraryPillVisible() ? libraryPill : settingsPill;
      drawFocusRing(renderer, pill.x, pill.y, pill.w, pill.h, 23.0f,
                    focusOpacity);
    }
  }

  const TextId sectionTitle =
      libraryEmpty
          ? TextId::GameLibrary
          : (startupPage == 0 ? TextId::ContinuePlaying : TextId::GameLibrary);
  const TextId sectionSubtitle =
      libraryEmpty ? (libraryPillVisible() ? TextId::ChooseRom
                                           : TextId::ChooseRomHandheld)
                   : (startupPage == 0 ? TextId::ContinueSubtitle
                                       : TextId::LibrarySubtitle);
  text(renderer, static_cast<int>(layout.marginX),
       static_cast<int>(layout.sectionLabelY), tr(sectionTitle, language), 1,
       style.accent);
  text(renderer, static_cast<int>(layout.marginX),
       static_cast<int>(layout.sectionTitleY),
       tr(TextId::ChooseYourGame, language), 3, {255, 255, 255, 255});
  text(renderer, static_cast<int>(layout.marginX),
       static_cast<int>(layout.sectionSubtitleY),
       tr(sectionSubtitle, language), 1, {191, 201, 220, 255});

  const bool cardsFocused = focus == HomeFocus::Cards;
  // The shelf is a window, not a canvas: a card that sticks out past the row is
  // cut there, which is what turns the eighth card into the half card that
  // shows there is more to the right.
  setClipRect(renderer, layout.rowLeft, layout.shelfTop,
              layout.rowRight - layout.rowLeft,
              layout.shelfBottom + 18.0f - layout.shelfTop);
  // Draw one column beyond each edge so a card sliding out and the next one
  // sliding in both stay whole while the row travels.
  const int firstColumn = static_cast<int>(std::floor(rowOffset));
  // Always fill the window, including the half column at the right edge: an
  // empty shelf still shows half a placeholder there, so the row reads as a
  // shelf that continues rather than one that simply stops.
  const int lastColumn =
      firstColumn + static_cast<int>(std::ceil(layout.visibleSlots));
  for (int i = std::max(0, firstColumn - 1); i <= lastColumn; ++i) {
    if (i == selectedGame || i > moreSlot)
      continue;
    if (i == moreSlot) {
      drawMoreTile(renderer, layout, static_cast<float>(i) - rowOffset, false,
                   false, focusOpacity, accent, language);
      continue;
    }
    const library::GameEntry *entry =
        i < static_cast<int>(gameLibrary.entries().size())
            ? &gameLibrary.entries()[static_cast<std::size_t>(i)]
            : nullptr;
    drawCard(renderer, layout, static_cast<float>(i) - rowOffset, false,
             false, focusOpacity, accent, entry, covers);
  }
  if (moreSelected)
    drawMoreTile(renderer, layout,
                 static_cast<float>(moreSlot) - rowOffset, true, cardsFocused,
                 focusOpacity, accent, language);
  else
    drawCard(renderer, layout, static_cast<float>(selectedGame) - rowOffset,
             true, cardsFocused, focusOpacity, accent, selectedEntry, covers);
  clearClipRect(renderer);

  // A toast takes the caption's place instead of covering the play time: the
  // two lines would otherwise be laid on top of each other whenever the
  // importer reports back.
  const bool toastVisible =
      SDL_GetTicks64() < toastUntil && !toastMessage.empty();
  if ((selectedEntry != nullptr || moreSelected) && !toastVisible) {
    const std::string caption =
        moreSelected ? tr(TextId::MoreGames, language) : selectedEntry->title;
    centeredText(renderer, static_cast<int>(layout.centerX),
                 static_cast<int>(layout.captionY),
                 fitText(caption, static_cast<int>(layout.captionWidth), 3), 3,
                 style.accent);
    const std::string detail =
        moreSelected
            ? std::string(tr(TextId::MoreSubtitle, language))
            : std::string(tr(TextId::PlayTime, language)) + ": " +
                  i18n::durationText(selectedEntry->playSeconds, language);
    centeredText(renderer, static_cast<int>(layout.centerX),
                 static_cast<int>(layout.playTimeY), detail, 1,
                 {232, 236, 244, 255});
  }

  // The shelf is the only screen with a legend of its own; the management
  // drawer covers it, so the caller can turn it off. Nothing here takes focus,
  // so the row is always drawn idle.
  if (showFooter) {
    fill(renderer, layout.rowLeft, layout.dividerY,
         layout.rowRight - layout.rowLeft, 1, {255, 255, 255, 24});
    const std::vector<Hint> hints = footerHints(
        selectedEntry != nullptr, moreSelected, language, prompts);
    drawActionRow(renderer, layout.footerRight, layout.footerY, hints);
  }

  if (toastVisible) {
    rounded(renderer, layout.toastX, layout.captionY, layout.toastWidth, 53, 14,
            {4, 8, 22, 235});
    centeredText(renderer, static_cast<int>(layout.centerX),
                 static_cast<int>(layout.captionY + 15),
                 std::string(toastMessage), 2, style.accent);
  }
}


int homeSlotCount(const library::GameLibrary &library) {
  // Ten cards plus the tile that closes the row. Movement wraps here, so the
  // tile is reachable with the right key and the empty cards are not skipped.
  return static_cast<int>(library.slotCount()) + 1;
}

std::vector<std::size_t> recentOrder(const library::GameLibrary &library) {
  const auto &entries = library.entries();
  std::vector<std::size_t> order(entries.size());
  for (std::size_t index = 0; index < order.size(); ++index)
    order[index] = index;
  std::stable_sort(order.begin(), order.end(),
                   [&entries](std::size_t left, std::size_t right) {
                     if (entries[left].lastPlayed != entries[right].lastPlayed)
                       return entries[left].lastPlayed >
                              entries[right].lastPlayed;
                     std::string leftTitle = entries[left].title;
                     std::string rightTitle = entries[right].title;
                     for (char &c : leftTitle)
                       c = static_cast<char>(std::tolower(
                           static_cast<unsigned char>(c)));
                     for (char &c : rightTitle)
                       c = static_cast<char>(std::tolower(
                           static_cast<unsigned char>(c)));
                     return leftTitle < rightTitle;
                   });
  return order;
}

namespace {
struct MoreLayout {
  float marginX = 0.0f;
  float rowRight = 0.0f;
  float gridTop = 0.0f;
  float gridBottom = 0.0f;
  float gap = 0.0f;
  float cardWidth = 0.0f;
  float cardHeight = 0.0f;
  int columns = 0;
  int rows = 0;
};

MoreLayout moreLayout() {
  const float width = static_cast<float>(kCanvasWidth);
  const float height = static_cast<float>(kCanvasHeight);
  MoreLayout layout;
  layout.marginX = std::round(width * (58.0f / 1280.0f));
  layout.rowRight = width - layout.marginX;
  layout.gridTop = 202.0f;
  layout.gridBottom = height - 132.0f;
  layout.gap = std::max(10.0f, std::round(width * (26.0f / 1280.0f)));
  const float span = layout.rowRight - layout.marginX;
  const float area = layout.gridBottom - layout.gridTop;
  // Pick the column count that yields the biggest card that still leaves two
  // or more rows. Sizing cards to the row height alone leaves the sideways
  // gaps as wide as a card, which is what made the wall look sparse.
  for (int columns = 3; columns <= 8; ++columns) {
    const float cardWidth = (span / static_cast<float>(columns)) - layout.gap;
    if (cardWidth <= 0.0f)
      continue;
    const float cardHeight = cardWidth * (314.0f / 226.0f);
    const int rows = static_cast<int>(std::floor(
        (area + layout.gap) / (cardHeight + layout.gap)));
    if (rows < 2 || rows > 5)
      continue;
    if (cardWidth > layout.cardWidth) {
      layout.columns = columns;
      layout.rows = rows;
      layout.cardWidth = cardWidth;
      layout.cardHeight = cardHeight;
    }
  }
  if (layout.cardWidth <= 0.0f) {
    // Nothing fits two rows at full width: fall back to the height bound.
    layout.rows = 2;
    layout.cardHeight = (area / 2.0f) - layout.gap;
    layout.cardWidth = layout.cardHeight * (226.0f / 314.0f);
    layout.columns = std::max(
        3, static_cast<int>(std::floor((span + layout.gap) /
                                      (layout.cardWidth + layout.gap))));
  }
  return layout;
}

SDL_FRect moreCardBox(const MoreLayout &layout, int column, int row) {
  const float gridWidth = layout.columns * layout.cardWidth +
                          (layout.columns - 1) * layout.gap;
  const float gridHeight = layout.rows * layout.cardHeight +
                           (layout.rows - 1) * layout.gap;
  const float x0 = layout.marginX +
                   ((layout.rowRight - layout.marginX) - gridWidth) * 0.5f;
  const float y0 = layout.gridTop +
                   ((layout.gridBottom - layout.gridTop) - gridHeight) * 0.5f;
  return {x0 + static_cast<float>(column) * (layout.cardWidth + layout.gap),
          y0 + static_cast<float>(row) * (layout.cardHeight + layout.gap),
          layout.cardWidth, layout.cardHeight};
}

int pageStart(int cursor) {
  const MoreGrid grid = moreGrid();
  const int pageSize = std::max(1, grid.columns * grid.rows);
  return (cursor / pageSize) * pageSize;
}
} // namespace

MoreGrid moreGrid() {
  const MoreLayout layout = moreLayout();
  return {layout.columns, layout.rows};
}

int moreGameAt(float x, float y, int count, int cursor) {
  if (count <= 0)
    return -1;
  const MoreLayout layout = moreLayout();
  const int start = pageStart(cursor);
  const int cells = layout.columns * layout.rows;
  for (int slot = start; slot < std::min(count, start + cells); ++slot) {
    const int local = slot - start;
    const SDL_FRect box =
        moreCardBox(layout, local % layout.columns, local / layout.columns);
    if (x >= box.x && x <= box.x + box.w && y >= box.y && y <= box.y + box.h)
      return slot;
  }
  return -1;
}

void drawMoreGames(SDL_Renderer *renderer,
                   const library::GameLibrary &gameLibrary,
                   const std::vector<std::size_t> &order, int cursor,
                   i18n::Language language,
                   const config::InputPrompts &prompts, float focusOpacity,
                   SDL_Color accent, CoverSource *covers) {
  using i18n::TextId;
  using i18n::tr;
  const MoreLayout layout = moreLayout();
  const int count = static_cast<int>(order.size());
  cursor = count > 0 ? std::clamp(cursor, 0, count - 1) : 0;
  const int start = pageStart(cursor);
  const int pageSize = std::max(1, layout.columns * layout.rows);
  const int pageCount = std::max(1, (count + pageSize - 1) / pageSize);
  const int page = count > 0 ? cursor / pageSize : 0;
  const int columns = layout.columns;

  Game style{};
  style.top = kCardTop;
  style.bottom = kCardBottom;
  style.accent = accent;
  // The wall is a plain page of art: no brand row and no header pills, so the
  // covers start high and the page keeps its height for the grid.
  drawBackground(renderer, style, nullptr, /*scene=*/false);

  text(renderer, static_cast<int>(layout.marginX), 96,
       tr(TextId::GameLibrary, language), 1, accent);
  text(renderer, static_cast<int>(layout.marginX), 124,
       tr(TextId::MoreTitle, language), 3, {255, 255, 255, 255});
  text(renderer, static_cast<int>(layout.marginX), 162,
       tr(TextId::MoreSubtitle, language), 1, {191, 201, 220, 255});

  const std::string sortLabel = tr(TextId::MoreSort, language);
  text(renderer,
       static_cast<int>(layout.rowRight) - textWidth(sortLabel, 1), 96,
       sortLabel, 1, {191, 201, 220, 255});
  const std::string pageLabel =
      std::to_string(page + 1) + " / " + std::to_string(pageCount);
  text(renderer,
       static_cast<int>(layout.rowRight) - textWidth(pageLabel, 1), 126,
       pageLabel, 1, {191, 201, 220, 255});

  for (int slot = start;
       slot < std::min(count, start + layout.columns * layout.rows); ++slot) {
    const library::GameEntry &entry =
        gameLibrary.entries()[order[static_cast<std::size_t>(slot)]];
    const int local = slot - start;
    const SDL_FRect box =
        moreCardBox(layout, local % columns, local / columns);
    const float radius = 14.0f;
    SDL_Texture *art = covers != nullptr ? covers->cover(renderer, entry)
                                        : nullptr;
    if (art != nullptr) {
      drawTexture(renderer, art, box.x, box.y, box.w, box.h);
    } else {
      drawCardPanel(renderer, box.x, box.y, box.w, box.h, radius, kCardTop,
                    kCardBottom, 255);
    }
    const char *badgeLabel = platformBadge(entry.platform);
    const float badgeHeight = 18.0f;
    const float badgeWidth =
        std::max(badgeHeight,
                 static_cast<float>(textWidth(badgeLabel, 1)) + 10.0f);
    const float badgeX = box.x + box.w - 8.0f - badgeWidth;
    const float badgeY = box.y + 8.0f;
    rounded(renderer, badgeX, badgeY, badgeWidth, badgeHeight,
            badgeHeight * 0.5f, accent);
    centeredGlyph(renderer, static_cast<int>(badgeX + badgeWidth * 0.5f),
                  static_cast<int>(badgeY + badgeHeight * 0.5f), badgeLabel, 1,
                  {10, 15, 28, 255});
    if (slot == cursor)
      drawFocusRing(renderer, box.x, box.y, box.w, box.h, radius,
                    focusOpacity);
  }

  if (count == 0) {
    // A wall with nothing on it still has to say why: the tile is reachable
    // even when the library is empty.
    const TextId emptyHint = libraryPillVisible()
                                 ? TextId::ChooseRom
                                 : TextId::ChooseRomHandheld;
    centeredText(renderer,
                 static_cast<int>((layout.marginX + layout.rowRight) * 0.5f),
                 static_cast<int>((layout.gridTop + layout.gridBottom) * 0.5f),
                 tr(emptyHint, language), 2, {191, 201, 220, 255});
  }

  // The one line under the grid says which game the ring is on: the covers are
  // unlabelled by design, so the page names the selection instead of printing a
  // caption under every card.
  const float dividerY = static_cast<float>(kCanvasHeight) - 82.0f;
  if (count > 0) {
    const library::GameEntry &selected =
        gameLibrary.entries()[order[static_cast<std::size_t>(cursor)]];
    const std::string playTime =
        std::string(tr(TextId::PlayTime, language)) + ": " +
        i18n::durationText(selected.playSeconds, language);
    const float playWidth = static_cast<float>(textWidth(playTime, 1));
    const int titleRoom = static_cast<int>(
        layout.rowRight - layout.marginX - playWidth - 28.0f);
    // One line, both ends: the title takes the space the play time leaves, so
    // neither runs into the divider under it.
    const int titleY = static_cast<int>(dividerY) - 44;
    text(renderer, static_cast<int>(layout.marginX), titleY,
         fitText(selected.title, titleRoom, 3), 3, accent);
    text(renderer,
         static_cast<int>(layout.rowRight - playWidth), titleY + 12,
         playTime, 1, {232, 236, 244, 255});
  }

  fill(renderer, layout.marginX, dividerY, layout.rowRight - layout.marginX, 1,
       {255, 255, 255, 24});
  std::vector<Hint> hints;
  hints.push_back({tr(TextId::HintOptions, language), prompts.options, false});
  hints.push_back({tr(TextId::Back, language), prompts.back, false});
  hints.push_back(
      {tr(TextId::ConfirmControl, language), prompts.confirm, true});
  drawActionRow(renderer, layout.rowRight,
                static_cast<float>(kCanvasHeight) - 62.0f, hints);
}

bool libraryPillVisible() {
#if defined(MRG_PLATFORM_TRIMUI)
  return false;
#else
  return true;
#endif
}

int homePillAt(float x, float y, i18n::Language language, std::size_t count) {
  const HomeLayout layout = homeLayout();
  if (y < layout.headerY - 3.0f ||
      y > layout.headerY + layout.headerHeight + 3.0f)
    return -1;
  if (libraryPillVisible()) {
    const SDL_FRect libraryPill = pillRect(layout, false, language, count);
    if (x >= libraryPill.x && x <= libraryPill.x + libraryPill.w)
      return 0;
  }
  const SDL_FRect settingsPill = pillRect(layout, true, language, count);
  if (x >= settingsPill.x && x <= settingsPill.x + settingsPill.w)
    return 1;
  return -1;
}

int homeActionAt(float x, float y, bool hasGame, bool moreSelected,
                 i18n::Language language,
                 const config::InputPrompts &prompts) {
  const HomeLayout home = homeLayout();
  const auto layout =
      actionLayout(home.footerRight, home.footerY,
                   footerHints(hasGame, moreSelected, language, prompts));
  for (std::size_t index = 0; index < layout.size(); ++index) {
    // Retain a little pointer padding around the small frames.
    if (layout[index].expanded(4, 6).contains(x, y)) {
      // Drawing order puts the drawer first; the ids keep start at zero so the
      // mouse and the pad speak the same numbers.
      return static_cast<int>(layout.size() - 1 - index);
    }
  }
  return -1;
}

int homeCardAt(float x, float y, int count, float rowOffset) {
  // The selected card is drawn a little larger, so accept either size and a
  // click on its rim still counts.
  const HomeLayout layout = homeLayout();
  const int firstColumn = static_cast<int>(std::floor(rowOffset));
  const int lastSlot = std::min(
      count - 1, firstColumn + static_cast<int>(layout.visibleSlots) + 1);
  for (int slot = std::max(0, firstColumn - 1); slot <= lastSlot; ++slot) {
    for (const bool selected : {false, true}) {
      const CardBox box =
          cardBox(layout, static_cast<float>(slot) - rowOffset, selected);
      if (x >= box.x && x <= box.x + box.width && y >= box.y &&
          y <= box.y + box.height)
        return slot;
    }
  }
  return -1;
}

float homeRowTarget(int selectedGame, int count) {
  // The row stops at the last position instead of wrapping. The window shows
  // half a slot past the last whole one, so the travel has to stop one slot
  // earlier than that half: otherwise the final card - the MORE GAMES tile -
  // could never be scrolled fully into view.
  const float slots = homeLayout().visibleSlots;
  const float whole = std::floor(slots);
  const float overflow = std::max(0.0f, static_cast<float>(count) - whole);
  const float start = std::clamp(
      static_cast<float>(selectedGame) - (slots - 1.5f), 0.0f, overflow);
  return start;
}

void drawQuitConfirm(SDL_Renderer *renderer, int choice,
                     i18n::Language language, SDL_Color accent) {
  using i18n::TextId;
  const ConfirmLayout layout = quitLayout();
  drawConfirmFrame(renderer, layout, tr(TextId::QuitConfirmTitle, language),
                   tr(TextId::QuitConfirmBody, language), accent, 160);
  drawChoiceButtons(renderer, layout.buttons,
                    {tr(TextId::Cancel, language), tr(TextId::Quit, language)},
                    choice, accent);
  // A dialog is just its two buttons: cancel on the left, confirm on the right,
  // and no key glyphs underneath. Repeating the keys here would label the same
  // action twice and crowd a box that is narrow by design.
}

int quitConfirmChoiceAt(float x, float y) {
  return quitLayout().buttons.hit(x, y);
}

void moveSelection(int &selectedGame, int direction, int count) {
  if (count <= 0)
    return;
  selectedGame = (selectedGame + direction + count) % count;
}

} // namespace launcher::ui
