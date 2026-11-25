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

const PanelLayout kPanel = panelLayout(
    {976, 64, 552, 772}, Rect{976, 64, 552, 772}.inset(36, 40, 36, 4),
    114, 26, 16);
const LinearLayout kActions{{kPanel.content.x, 396, kPanel.content.w, 44},
                            kPauseActionCount, 4};
const LinearLayout kVideoRows{{kPanel.content.x, 300, kPanel.content.w, 70},
                              kPauseVideoRowCount, 10};
const LinearLayout kOptionRows{{kPanel.content.x, 300, kPanel.content.w, 50},
                               kPauseCoreOptionRows, 6};
const LinearLayout kSlots = equalColumns(
    {kPanel.content.x, 276, kPanel.content.w, 58}, kStateSlotCount, 12);
// Reserve separate vertical space for the buttons and footer (previously they
// overlapped). The row remains two equal-width choices.
const ConfirmLayout kPowerLayout =
    confirmLayout({460, 300, 680, 320}, 170, 48, 24, 20);

void drawSidebar(SDL_Renderer *renderer, const std::string &subtitle,
                 i18n::Language language, SDL_Color accent) {
  fill(renderer, 0, 0, kLogicalWidth, kLogicalHeight, {2, 5, 12, 64});
  drawPanel(renderer, kPanel.bounds, 28, {12, 21, 37, 225},
            kPanel.bounds.expanded(8, 0).translated(0, 8), {0, 0, 0, 45});
  const Rect header = kPanel.header;
  rounded(renderer, header.x, header.y, 4, 32, 2, accent);
  text(renderer, header.x + 20, header.y + 2,
       tr(TextId::PauseTitle, language), 2, accent);
  text(renderer, header.x, header.y + 54,
       fitText(subtitle, static_cast<int>(header.w), 3), 3,
       {245, 247, 252, 255});
  fill(renderer, header.x, header.bottom(), header.w, 1,
       {255, 255, 255, 30});
}

void drawSidebarFooter(SDL_Renderer *renderer, const std::vector<Hint> &hints,
                       SDL_Color accent) {
  fill(renderer, kPanel.footer.x, kPanel.footer.y - 16, kPanel.footer.w, 1,
       {255, 255, 255, 30});
  drawHintRow(renderer, kPanel.footer.right(), kPanel.footer.y, hints, accent);
}

} // namespace

void drawPauseMenu(
    SDL_Renderer *renderer, std::string_view gameTitle, int selectedAction,
    int stateSlot,
    const std::array<StateSlotView, kStateSlotCount> &slots,
    std::string_view feedback, Uint64 feedbackUntil, i18n::Language language,
    const config::InputPrompts &prompts, SDL_Color accent) {
  const StateSlotView &current = slots[static_cast<std::size_t>(
      std::clamp(stateSlot, 0, kStateSlotCount - 1))];
  drawSidebar(renderer, std::string(gameTitle), language, accent);
  text(renderer, 1012, 240, tr(TextId::StateSlot, language), 1,
       {174, 188, 208, 255});
  for (int slot = 0; slot < kStateSlotCount; ++slot) {
    const Rect bounds = kSlots.item(slot);
    const float x = bounds.x;
    const bool selected = slot == stateSlot;
    rounded(renderer, bounds.x, bounds.y, bounds.w, bounds.h, 12,
            selected ? accent
                     : SDL_Color{255, 255, 255, 18});
    centeredText(
        renderer, static_cast<int>(x + bounds.w / 2), 291, std::to_string(slot + 1), 2,
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
              accent);

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
      drawTexture(renderer, current.thumbnail, kCardX + 6.0f, kCardY + 6.0f,
                  256.0f, 240.0f);
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
           accent);
    }
  }
  const std::array<TextId, kPauseActionCount> labels{
      TextId::ResumeGame,   TextId::SaveState,    TextId::LoadState,
      TextId::ConsoleReset, TextId::PowerCycle,   TextId::Display,
      TextId::CoreOptions,  TextId::ReturnHome};
  const std::array<const char *, kPauseActionCount> glyphs{
      ">", "S", "L", "R", "P", "D", "O", "<"};
  for (int action = 0; action < kPauseActionCount; ++action) {
    const Rect bounds = kActions.item(action);
    const float y = bounds.y;
    const bool selected = action == selectedAction;
    drawFocusablePlate(renderer, bounds.x, bounds.y, bounds.w, bounds.h, 16,
                       selected ? accent : SDL_Color{255, 255, 255, 16},
                       selected);
    circle(renderer, 1048, static_cast<int>(y + 22), 14,
           selected ? SDL_Color{25, 25, 41, 255}
                    : SDL_Color{255, 255, 255, 26});
    centeredGlyph(renderer, 1048, static_cast<int>(y + 22), glyphs[action], 2,
                  selected ? accent
                           : SDL_Color{210, 218, 232, 255});
    text(renderer, 1084, static_cast<int>(y + 13), tr(labels[action], language),
         2,
         selected ? SDL_Color{24, 24, 39, 255} : SDL_Color{255, 255, 255, 255});
  }

  // Two keys only: the one that acts and the one that leaves. Which key moves
  // between the entries is never written down.
  const std::vector<Hint> footer{
      {tr(TextId::ResumeGame, language), prompts.back, false},
      {tr(TextId::ConfirmControl, language), prompts.confirm, true}};
  drawSidebarFooter(renderer, footer, accent);

  if (SDL_GetTicks64() < feedbackUntil && !feedback.empty()) {
    rounded(renderer, 180, 760, 620, 58, 18, {3, 8, 20, 245});
    centeredText(renderer, 490, 776, std::string(feedback), 2,
                 accent);
  }
}

int pauseSlotAt(float x, float y) {
  return kSlots.hit(x, y);
}

void drawPowerCycleConfirm(SDL_Renderer *renderer, int choice,
                           i18n::Language language,
                           const config::InputPrompts &prompts,
                           SDL_Color accent) {
  drawConfirmFrame(renderer, kPowerLayout, tr(TextId::PowerCycleTitle, language),
                   tr(TextId::PowerCycleBody, language), accent, 150);
  drawChoiceButtons(renderer, kPowerLayout.buttons,
                    {tr(TextId::Cancel, language),
                     tr(TextId::PowerCycle, language)}, choice, accent);
  // Cancel sits on the left button, so its key is written first.
  const std::vector<Hint> footer{
      {tr(TextId::Cancel, language), prompts.back, false},
      {tr(TextId::ConfirmControl, language), prompts.confirm, true}};
  drawHintRow(renderer, kPowerLayout.footer.right(), kPowerLayout.footer.y,
              footer, accent);
}

int powerCycleChoiceAt(float x, float y) {
  return kPowerLayout.buttons.hit(x, y);
}

int pauseVideoRowAt(float x, float y) {
  return kVideoRows.hit(x, y);
}

void drawPauseVideoPanel(SDL_Renderer *renderer, int row,
                         const PauseVideoState &video, i18n::Language language,
                         const config::InputPrompts &prompts,
                         SDL_Color accent) {
  drawSidebar(renderer, tr(TextId::GameDisplayTitle, language), language, accent);

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
    const Rect bounds = kVideoRows.item(index);
    const float y = bounds.y;
    const bool selected = index == row;
    drawFocusablePlate(renderer, bounds.x, bounds.y, bounds.w, bounds.h,
                       14,
                       selected ? accent : SDL_Color{255, 255, 255, 16},
                       selected);
    text(renderer, 1032, static_cast<int>(y + 12), titles[index], 2,
         selected ? SDL_Color{24, 24, 39, 255}
                  : SDL_Color{241, 245, 251, 255});
    text(renderer, 1472 - textWidth(values[index], 2),
         static_cast<int>(y + 12), values[index], 2,
         selected ? SDL_Color{24, 24, 39, 255}
                  : accent);
    if (index + 1 == kPauseVideoRowCount) {
      text(renderer, 1032, static_cast<int>(y + 42),
           tr(TextId::UseGlobalDefaultsDescription, language), 1,
           selected ? SDL_Color{70, 57, 39, 255}
                    : SDL_Color{165, 181, 204, 255});
    }
  }

  text(renderer, 1012, 642,
       fitText(tr(TextId::UseGlobalDefaultsHint, language), 480, 1), 1,
       {165, 181, 204, 255});

  const std::vector<Hint> footer{
      {tr(TextId::Back, language), prompts.back, false},
      {tr(TextId::ConfirmControl, language), prompts.confirm, true}};
  drawSidebarFooter(renderer, footer, accent);
}

int pauseActionAt(float x, float y) {
  return kActions.hit(x, y);
}

int pauseCoreOptionAt(float x, float y, int scroll) {
  const int row = kOptionRows.hit(x, y);
  return row < 0 ? -1 : scroll + row;
}

void drawPauseCoreOptionsPanel(SDL_Renderer *renderer, int cursor, int scroll,
                               const runtime::CoreOptionList &options,
                               i18n::Language language,
                               const config::InputPrompts &prompts,
                               SDL_Color accent) {
  drawSidebar(renderer, tr(TextId::CoreOptions, language), language, accent);

  const int total = static_cast<int>(options.size());
  if (total == 0) {
    text(renderer, 1012, 320,
         fitText(tr(TextId::CoreOptionsEmpty, language), 480, 2), 2,
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
      const Rect bounds = kOptionRows.item(row);
      const float y = bounds.y;
      drawFocusablePlate(renderer, bounds.x, bounds.y, bounds.w, bounds.h, 14,
                         selected ? accent : SDL_Color{255, 255, 255, 16},
                         selected);
      const std::string label =
          i18n::coreOptionLabel(option.key, option.label, language);
      text(renderer, 1032, static_cast<int>(y + 15),
           fitText(label, 240, 2), 2,
           selected ? SDL_Color{24, 24, 39, 255}
                    : SDL_Color{241, 245, 251, 255});
      std::string value;
      if (option.selected < option.values.size()) {
        const runtime::CoreOptionValue &entry = option.values[option.selected];
        value = i18n::coreOptionValue(entry.value, entry.label, language);
      }
      value = fitText(value, 190, 2);
      text(renderer, 1472 - textWidth(value, 2), static_cast<int>(y + 15),
           value, 2,
           selected ? SDL_Color{24, 24, 39, 255}
                    : accent);
    }
    text(renderer, 1012, 660,
         std::to_string(cursor + 1) + " / " + std::to_string(total), 1,
         {165, 181, 204, 255});
  }

  drawHintRow(renderer, 1492.0f, 700.0f,
              {{tr(TextId::ResetCoreOptions, language), "R", false}},
              accent);
  const std::vector<Hint> footer{
      {tr(TextId::Back, language), prompts.back, false},
      {tr(TextId::ConfirmControl, language), prompts.confirm, true}};
  drawSidebarFooter(renderer, footer, accent);
}

} // namespace launcher::ui
