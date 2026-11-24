#include "ui/PauseMenuScreen.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "i18n/CoreOptionText.hpp"
#include "i18n/Localization.hpp"
#include "ui/Primitives.hpp"

namespace launcher::ui {
namespace {
using i18n::TextId;
using i18n::tr;

constexpr float kActionX = 1012.0f;
constexpr float kActionY = 396.0f;
constexpr float kActionWidth = 480.0f;
constexpr float kActionHeight = 44.0f;
constexpr float kActionStep = 48.0f;

constexpr float kVideoRowY = 300.0f;
constexpr float kVideoRowStep = 80.0f;
constexpr float kVideoRowHeight = 70.0f;

constexpr float kOptionRowY = 300.0f;
constexpr float kOptionRowStep = 56.0f;
constexpr float kOptionRowHeight = 50.0f;

constexpr float kConfirmX = 460.0f;
constexpr float kConfirmY = 300.0f;
constexpr float kConfirmWidth = 680.0f;
constexpr float kConfirmHeight = 300.0f;
constexpr float kConfirmButtonY = 500.0f;
constexpr float kConfirmButtonWidth = 280.0f;
constexpr float kConfirmButtonHeight = 64.0f;
constexpr float kConfirmCancelX = 516.0f;
constexpr float kConfirmPowerX = 820.0f;

std::string fitLine(std::string value, int width, int scale) {
  if (textWidth(value, scale) <= width)
    return value;
  const std::string suffix = "...";
  while (!value.empty() && textWidth(value + suffix, scale) > width) {
    std::size_t start = value.size() - 1;
    while (start > 0 &&
           (static_cast<unsigned char>(value[start]) & 0xc0) == 0x80)
      --start;
    value.resize(start);
  }
  return value + suffix;
}

} // namespace

void drawPauseMenu(
    SDL_Renderer *renderer, std::string_view gameTitle, int selectedAction,
    int stateSlot,
    const std::array<StateSlotView, kStateSlotCount> &slots,
    std::string_view feedback, Uint64 feedbackUntil, i18n::Language language,
    const config::InputPrompts &prompts) {
  const StateSlotView &current = slots[static_cast<std::size_t>(
      std::clamp(stateSlot, 0, kStateSlotCount - 1))];
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {2, 5, 12, 64});
  rounded(renderer, 968, 72, 568, 772, 28, {0, 0, 0, 45});
  rounded(renderer, 976, 64, 552, 772, 28, {12, 21, 37, 225});
  rounded(renderer, 1012, 104, 4, 32, 2, {255, 211, 101, 255});
  text(renderer, 1032, 106, tr(TextId::PauseTitle, language), 2,
       {255, 211, 101, 255});
  text(renderer, 1012, 158, fitLine(std::string(gameTitle), 480, 3), 3,
       {245, 247, 252, 255});
  fill(renderer, 1012, 218, 480, 1, {255, 255, 255, 30});
  text(renderer, 1012, 240, tr(TextId::StateSlot, language), 1,
       {174, 188, 208, 255});
  for (int slot = 0; slot < kStateSlotCount; ++slot) {
    const float x = 1012.0f + slot * 164.0f;
    const bool selected = slot == stateSlot;
    rounded(renderer, x, 276, 152, 58, 12,
            selected ? SDL_Color{255, 211, 101, 255}
                     : SDL_Color{255, 255, 255, 18});
    centeredText(
        renderer, static_cast<int>(x + 76), 291, std::to_string(slot + 1), 2,
        selected ? SDL_Color{23, 23, 38, 255} : SDL_Color{230, 235, 245, 255});
  }
  circle(renderer, 1017, 366, 4,
         current.present ? SDL_Color{132, 220, 157, 255}
                         : SDL_Color{142, 155, 179, 255});
  text(renderer, 1032, 355,
       tr(current.present ? TextId::StateAvailable : TextId::StateEmpty,
          language),
       1, {184, 204, 212, 255});
  // The frame-step legend names the action first, then the binding; it sits on
  // the slot heading row so it never crowds the action list.
  drawHintRow(renderer, 1492.0f, 234.0f,
              {{tr(TextId::FrameStep, language), prompts.frameStep, false}},
              {255, 211, 101, 255});

  // Thumbnail card for the focused slot, parked over the dimmed game picture.
  if (current.present) {
    constexpr float kCardX = 676.0f;
    constexpr float kCardY = 276.0f;
    constexpr float kCardWidth = 268.0f;
    constexpr float kCardHeight = 310.0f;
    rounded(renderer, kCardX, kCardY, kCardWidth, kCardHeight, 20,
            {8, 16, 31, 245});
    rounded(renderer, kCardX + 6, kCardY + 6, kCardWidth - 12, 240, 14,
            {3, 7, 18, 255});
    if (current.thumbnail != nullptr) {
      const SDL_FRect destination{kCardX + 6.0f, kCardY + 6.0f, 256.0f, 240.0f};
      SDL_RenderCopyF(renderer, current.thumbnail, nullptr, &destination);
    }
    if (!current.savedAt.empty()) {
      text(renderer, static_cast<int>(kCardX + 10.0f),
           static_cast<int>(kCardY + 256.0f),
           std::string(tr(TextId::StateSavedAt, language)) + " " +
               current.savedAt,
           1,
           {184, 204, 212, 255});
    }
    if (!current.playTime.empty()) {
      text(renderer, static_cast<int>(kCardX + 10.0f),
           static_cast<int>(kCardY + 280.0f), current.playTime, 1,
           {255, 211, 101, 255});
    }
  }
  const std::array<TextId, kPauseActionCount> labels{
      TextId::ResumeGame,   TextId::SaveState,    TextId::LoadState,
      TextId::ConsoleReset, TextId::PowerCycle,   TextId::Display,
      TextId::CoreOptions,  TextId::ReturnHome};
  const std::array<const char *, kPauseActionCount> glyphs{
      ">", "S", "L", "R", "P", "D", "O", "<"};
  for (int action = 0; action < kPauseActionCount; ++action) {
    const float y = kActionY + action * kActionStep;
    const bool selected = action == selectedAction;
    rounded(renderer, kActionX, y, kActionWidth, kActionHeight, 16,
            selected ? SDL_Color{255, 211, 101, 255}
                     : SDL_Color{255, 255, 255, 16});
    circle(renderer, 1048, static_cast<int>(y + 22), 14,
           selected ? SDL_Color{25, 25, 41, 255}
                    : SDL_Color{255, 255, 255, 26});
    centeredGlyph(renderer, 1048, static_cast<int>(y + 22), glyphs[action], 2,
                  selected ? SDL_Color{255, 211, 101, 255}
                           : SDL_Color{210, 218, 232, 255});
    text(renderer, 1084, static_cast<int>(y + 13), tr(labels[action], language),
         2,
         selected ? SDL_Color{24, 24, 39, 255} : SDL_Color{255, 255, 255, 255});
  }

  fill(renderer, 1012, 790, 480, 1, {255, 255, 255, 30});
  // Two keys only: the one that acts and the one that leaves. Which key moves
  // between the entries is never written down.
  const std::vector<Hint> footer{
      {tr(TextId::ResumeGame, language), prompts.back, false},
      {tr(TextId::ConfirmControl, language), prompts.confirm, true}};
  drawHintRow(renderer, 1492.0f, 806.0f, footer, {255, 211, 101, 255});

  if (SDL_GetTicks64() < feedbackUntil && !feedback.empty()) {
    rounded(renderer, 180, 760, 620, 58, 18, {3, 8, 20, 245});
    centeredText(renderer, 490, 776, std::string(feedback), 2,
                 {255, 211, 101, 255});
  }
}

int pauseSlotAt(float x, float y) {
  if (y < 276 || y >= 334)
    return -1;
  for (int slot = 0; slot < kStateSlotCount; ++slot) {
    const float left = 1012.0f + slot * 164.0f;
    if (x >= left && x < left + 152)
      return slot;
  }
  return -1;
}

void drawPowerCycleConfirm(SDL_Renderer *renderer, int choice,
                           i18n::Language language,
                           const config::InputPrompts &prompts) {
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {2, 5, 12, 150});
  rounded(renderer, kConfirmX + 8, kConfirmY + 10, kConfirmWidth,
          kConfirmHeight, 28, {0, 0, 0, 70});
  rounded(renderer, kConfirmX, kConfirmY, kConfirmWidth, kConfirmHeight, 28,
          {12, 21, 37, 245});
  rounded(renderer, kConfirmX + 36, kConfirmY + 40, 4, 32, 2,
          {255, 211, 101, 255});
  text(renderer, kConfirmX + 56, kConfirmY + 42,
       tr(TextId::PowerCycleTitle, language), 2, {255, 211, 101, 255});
  text(renderer, kConfirmX + 36, kConfirmY + 110,
       tr(TextId::PowerCycleBody, language), 2, {206, 216, 232, 255});

  const std::array<TextId, 2> labels{TextId::Cancel, TextId::PowerCycle};
  const std::array<float, 2> xs{kConfirmCancelX, kConfirmPowerX};
  for (int option = 0; option < 2; ++option) {
    const bool selected = option == choice;
    rounded(renderer, xs[static_cast<std::size_t>(option)], kConfirmButtonY,
            kConfirmButtonWidth, kConfirmButtonHeight, 18,
            selected ? SDL_Color{255, 211, 101, 255}
                     : SDL_Color{255, 255, 255, 18});
    centeredText(renderer,
                 static_cast<int>(xs[static_cast<std::size_t>(option)] +
                                  kConfirmButtonWidth / 2.0f),
                 static_cast<int>(kConfirmButtonY + 24),
                 tr(labels[static_cast<std::size_t>(option)], language), 2,
                 selected ? SDL_Color{24, 24, 39, 255}
                          : SDL_Color{230, 235, 245, 255});
    if (selected) {
      drawFocusRing(renderer, xs[static_cast<std::size_t>(option)],
                    kConfirmButtonY, kConfirmButtonWidth, kConfirmButtonHeight,
                    18.0f, 1.0f);
    }
  }
  // Cancel sits on the left button, so its key is written first.
  const std::vector<Hint> footer{
      {tr(TextId::Cancel, language), prompts.back, false},
      {tr(TextId::ConfirmControl, language), prompts.confirm, true}};
  drawHintRow(renderer, kConfirmX + kConfirmWidth - 36.0f,
              kConfirmY + kConfirmHeight - 52.0f, footer,
              {255, 211, 101, 255});
}

int powerCycleChoiceAt(float x, float y) {
  if (y < kConfirmButtonY || y > kConfirmButtonY + kConfirmButtonHeight)
    return -1;
  if (x >= kConfirmCancelX && x <= kConfirmCancelX + kConfirmButtonWidth)
    return 0;
  if (x >= kConfirmPowerX && x <= kConfirmPowerX + kConfirmButtonWidth)
    return 1;
  return -1;
}

int pauseVideoRowAt(float x, float y) {
  if (x < kActionX || x > kActionX + kActionWidth)
    return -1;
  for (int row = 0; row < kPauseVideoRowCount; ++row) {
    const float top = kVideoRowY + row * kVideoRowStep;
    if (y >= top && y <= top + kVideoRowHeight)
      return row;
  }
  return -1;
}

void drawPauseVideoPanel(SDL_Renderer *renderer, int row,
                         const PauseVideoState &video, i18n::Language language,
                         const config::InputPrompts &prompts) {
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {2, 5, 12, 64});
  rounded(renderer, 968, 72, 568, 772, 28, {0, 0, 0, 45});
  rounded(renderer, 976, 64, 552, 772, 28, {12, 21, 37, 225});
  rounded(renderer, 1012, 104, 4, 32, 2, {255, 211, 101, 255});
  text(renderer, 1032, 106, tr(TextId::PauseTitle, language), 2,
       {255, 211, 101, 255});
  text(renderer, 1012, 158, tr(TextId::GameDisplayTitle, language), 3,
       {245, 247, 252, 255});
  fill(renderer, 1012, 218, 480, 1, {255, 255, 255, 30});

  TextId aspectId = TextId::AspectPixel;
  if (video.aspect == 1)
    aspectId = TextId::AspectScreen4x3;
  else if (video.aspect == 2)
    aspectId = TextId::AspectPixels8x7;
  const std::string aspect = tr(aspectId, language);

  const std::array<std::string, kPauseVideoRowCount> titles{
      tr(TextId::AspectRatio, language), tr(TextId::IntegerScale, language),
      tr(TextId::Overscan, language), tr(TextId::UseGlobalDefaults, language)};
  const std::array<std::string, kPauseVideoRowCount> values{
      aspect, tr(video.integerScale ? TextId::On : TextId::Off, language),
      std::to_string(video.overscan) + " px",
      tr(video.overrideEnabled ? TextId::Off : TextId::On, language)};

  for (int index = 0; index < kPauseVideoRowCount; ++index) {
    const float y = kVideoRowY + index * kVideoRowStep;
    const bool selected = index == row;
    rounded(renderer, kActionX, y, kActionWidth, kVideoRowHeight, 14,
            selected ? SDL_Color{255, 211, 101, 255}
                     : SDL_Color{255, 255, 255, 16});
    text(renderer, 1032, static_cast<int>(y + 12), titles[index], 2,
         selected ? SDL_Color{24, 24, 39, 255}
                  : SDL_Color{241, 245, 251, 255});
    text(renderer, 1472 - textWidth(values[index], 2),
         static_cast<int>(y + 12), values[index], 2,
         selected ? SDL_Color{24, 24, 39, 255}
                  : SDL_Color{255, 211, 101, 255});
    if (index + 1 == kPauseVideoRowCount) {
      text(renderer, 1032, static_cast<int>(y + 42),
           tr(TextId::UseGlobalDefaultsDescription, language), 1,
           selected ? SDL_Color{70, 57, 39, 255}
                    : SDL_Color{165, 181, 204, 255});
    }
  }

  text(renderer, 1012, 642,
       fitLine(tr(TextId::UseGlobalDefaultsHint, language), 480, 1), 1,
       {165, 181, 204, 255});

  const std::vector<Hint> footer{
      {tr(TextId::Back, language), prompts.back, false},
      {tr(TextId::ConfirmControl, language), prompts.confirm, true}};
  drawHintRow(renderer, 1492.0f, 806.0f, footer, {255, 211, 101, 255});
}

int pauseActionAt(float x, float y) {
  if (x < kActionX || x > kActionX + kActionWidth)
    return -1;
  for (int action = 0; action < kPauseActionCount; ++action) {
    const float top = kActionY + action * kActionStep;
    if (y >= top && y <= top + kActionHeight)
      return action;
  }
  return -1;
}

int pauseCoreOptionAt(float x, float y, int scroll) {
  if (x < kActionX || x > kActionX + kActionWidth)
    return -1;
  for (int row = 0; row < kPauseCoreOptionRows; ++row) {
    const float top = kOptionRowY + row * kOptionRowStep;
    if (y >= top && y <= top + kOptionRowHeight)
      return scroll + row;
  }
  return -1;
}

void drawPauseCoreOptionsPanel(SDL_Renderer *renderer, int cursor, int scroll,
                               const runtime::CoreOptionList &options,
                               i18n::Language language,
                               const config::InputPrompts &prompts) {
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {2, 5, 12, 64});
  rounded(renderer, 968, 72, 568, 772, 28, {0, 0, 0, 45});
  rounded(renderer, 976, 64, 552, 772, 28, {12, 21, 37, 225});
  rounded(renderer, 1012, 104, 4, 32, 2, {255, 211, 101, 255});
  text(renderer, 1032, 106, tr(TextId::PauseTitle, language), 2,
       {255, 211, 101, 255});
  text(renderer, 1012, 158, tr(TextId::CoreOptions, language), 3,
       {245, 247, 252, 255});
  fill(renderer, 1012, 218, 480, 1, {255, 255, 255, 30});

  const int total = static_cast<int>(options.size());
  if (total == 0) {
    text(renderer, 1012, 320,
         fitLine(tr(TextId::CoreOptionsEmpty, language), 480, 2), 2,
         {165, 181, 204, 255});
  } else {
    const int offset =
        std::clamp(scroll, 0, std::max(0, total - kPauseCoreOptionRows));
    for (int row = 0; row < kPauseCoreOptionRows; ++row) {
      const int index = offset + row;
      if (index >= total)
        break;
      const runtime::CoreOption &option =
          options[static_cast<std::size_t>(index)];
      const bool selected = index == cursor;
      const float y = kOptionRowY + row * kOptionRowStep;
      rounded(renderer, kActionX, y, kActionWidth, kOptionRowHeight, 14,
              selected ? SDL_Color{255, 211, 101, 255}
                       : SDL_Color{255, 255, 255, 16});
      const std::string label =
          i18n::coreOptionLabel(option.key, option.label, language);
      text(renderer, 1032, static_cast<int>(y + 15),
           fitLine(label, 240, 2), 2,
           selected ? SDL_Color{24, 24, 39, 255}
                    : SDL_Color{241, 245, 251, 255});
      std::string value;
      if (option.selected < option.values.size()) {
        const runtime::CoreOptionValue &entry = option.values[option.selected];
        value = i18n::coreOptionValue(entry.value, entry.label, language);
      }
      value = fitLine(value, 190, 2);
      text(renderer, 1472 - textWidth(value, 2), static_cast<int>(y + 15),
           value, 2,
           selected ? SDL_Color{24, 24, 39, 255}
                    : SDL_Color{255, 211, 101, 255});
    }
    text(renderer, 1012, 660,
         std::to_string(cursor + 1) + " / " + std::to_string(total), 1,
         {165, 181, 204, 255});
  }

  drawHintRow(renderer, 1492.0f, 700.0f,
              {{tr(TextId::ResetCoreOptions, language), "R", false}},
              {255, 211, 101, 255});
  const std::vector<Hint> footer{
      {tr(TextId::Back, language), prompts.back, false},
      {tr(TextId::ConfirmControl, language), prompts.confirm, true}};
  drawHintRow(renderer, 1492.0f, 806.0f, footer, {255, 211, 101, 255});
}

} // namespace launcher::ui
