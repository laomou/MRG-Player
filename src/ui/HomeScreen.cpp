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

int wrappedDelta(int index, int selected, int count) {
  int delta = index - selected;
  if (delta > count / 2)
    delta -= count;
  if (delta < -count / 2)
    delta += count;
  return delta;
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

void drawCard(SDL_Renderer *renderer, int index, int delta, bool selected,
              float pulse, const library::GameEntry *entry) {
  const Game &style = styleFor(index);
  const float width = selected ? 302.0f : 226.0f;
  const float height = selected ? 382.0f : 314.0f;
  const float x = kLogicalWidth * 0.5f - width * 0.5f + delta * 286.0f;
  const float y = selected ? 296.0f - pulse * 3.0f
                           : 330.0f + std::min(18, std::abs(delta) * 8);
  const Uint8 alpha =
      selected ? 255
               : static_cast<Uint8>(std::max(58, 205 - std::abs(delta) * 55));

  if (selected) {
    rounded(renderer, x - 7, y - 7, width + 14, height + 14, 27,
            {style.accent.r, style.accent.g, style.accent.b,
             static_cast<Uint8>(135 + pulse * 55)});
  }
  const float radius = selected ? 22.0f : 18.0f;
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

  if (entry == nullptr)
    return;

  if (entry->favorite) {
    circle(renderer, static_cast<int>(x + width - 28), static_cast<int>(y + 28),
           16, {255, 220, 114, static_cast<Uint8>(alpha * 0.9f)});
    centeredText(renderer, static_cast<int>(x + width - 28),
                 static_cast<int>(y + 21), "*", 2, {18, 18, 33, alpha});
  }

  const float panelY = y + height * 0.60f;
  rounded(renderer, x + 12, panelY, width - 24, height * 0.40f - 12, 12,
          {6, 9, 26, static_cast<Uint8>(alpha * 0.78f)});
  circle(renderer, static_cast<int>(x + width * 0.5f),
         static_cast<int>(y + height * 0.32f), selected ? 43 : 34,
         {style.accent.r, style.accent.g, style.accent.b,
          static_cast<Uint8>(alpha * 0.85f)});
  const auto &platform = game::platformDescriptor(entry->platform);
  centeredText(renderer, static_cast<int>(x + width * 0.5f),
               static_cast<int>(y + height * 0.29f),
               std::string(platform.displayName), selected ? 3 : 2,
               {8, 14, 30, alpha});

  const int titleScale = selected ? 3 : 2;
  centeredText(renderer, static_cast<int>(x + width * 0.5f),
               static_cast<int>(y + height * 0.70f),
               fitTitle(entry->title, static_cast<int>(width - 28), titleScale),
               titleScale, {255, 255, 255, alpha});
  centeredText(renderer, static_cast<int>(x + width * 0.5f),
               static_cast<int>(y + height * 0.84f),
               std::string(platform.fileTypeLabel), 2,
               {style.accent.r, style.accent.g, style.accent.b, alpha});
}

} // namespace

void drawHome(SDL_Renderer *renderer, int selectedGame, float time,
              Uint64 toastUntil, std::string_view toastMessage, int startupPage,
              i18n::Language language, const library::GameLibrary &gameLibrary,
              const config::InputPrompts &prompts) {
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
  for (int distance = 3; distance >= 1; --distance) {
    for (int i = 0; i < slotCount; ++i) {
      const int delta = wrappedDelta(i, selectedGame, slotCount);
      if (std::abs(delta) == distance) {
        const library::GameEntry *entry =
            i < static_cast<int>(gameLibrary.entries().size())
                ? &gameLibrary.entries()[static_cast<std::size_t>(i)]
                : nullptr;
        drawCard(renderer, i, delta, false, pulse, entry);
      }
    }
  }
  drawCard(renderer, selectedGame, 0, true, pulse, selectedEntry);

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
  std::vector<Hint> footer{
      {tr(TextId::Start, language), prompts.confirm, true}};
  if (selectedEntry != nullptr)
    footer.push_back({tr(TextId::ManageGame, language), prompts.manage, false});
  footer.push_back({tr(TextId::Back, language), prompts.back, false});
  drawHintRow(renderer, 1528.0f, 822.0f, footer, style.accent);

  if (SDL_GetTicks64() < toastUntil && !toastMessage.empty()) {
    rounded(renderer, 484, 760, 632, 66, 18, {4, 8, 22, 235});
    centeredText(renderer, 800, 779, std::string(toastMessage), 2,
                 style.accent);
  }
}

void moveSelection(int &selectedGame, int direction, int count) {
  if (count <= 0)
    return;
  selectedGame = (selectedGame + direction + count) % count;
}

} // namespace launcher::ui
