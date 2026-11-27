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

// The drawer hugs the right edge and takes whatever height the canvas offers,
// so a taller device grows the list instead of scaling it. On the 1280x720
// desktop canvas every rectangle here is the old hand-written one.
struct PauseLayout {
  PanelLayout panel;
  LinearLayout actions;
  LinearLayout videoRows;
  LinearLayout optionRows;
  LinearLayout slots;
  ConfirmLayout power;
  Rect thumbnail;
  Rect toast;
};

const PauseLayout &pauseLayout() {
  static const PauseLayout layout = [] {
    constexpr float kMargin = 57.0f;
    constexpr float kPanelWidth = 442.0f;
    // The card is exactly as tall as its nine rows: it floats in the middle of
    // the canvas instead of running the full height with a key legend under the
    // last row.
    constexpr float kTopInset = 32.0f;
    constexpr float kBottomInset = 24.0f;
    constexpr float kHeaderHeight = 91.0f;
    constexpr float kGap = 13.0f;
    constexpr float kRowsTop = 130.0f;
    constexpr float kRowHeight = 32.0f;
    constexpr float kRowGap = 2.0f;
    const float rowsHeight =
        kPauseActionCount * kRowHeight + (kPauseActionCount - 1) * kRowGap;
    const float panelHeight = kTopInset + kHeaderHeight + 2 * kGap + kRowsTop +
                              rowsHeight + kBottomInset;
    const float width = static_cast<float>(kCanvasWidth);
    const float height = static_cast<float>(kCanvasHeight);
    const Rect bounds{width - kMargin - kPanelWidth, (height - panelHeight) / 2,
                      kPanelWidth, panelHeight};
    const PanelLayout panel = panelLayout(
        bounds, bounds.inset(29, kTopInset, 29, kBottomInset), kHeaderHeight, 0,
        kGap);
    const Rect &content = panel.content;
    PauseLayout out;
    out.panel = panel;
    out.slots = equalColumns({content.x, content.y + 34, content.w, 46},
                             kStateSlotCount, 10);
    // Nine rows fit without touching the footer, even on the shorter desktop.
    out.actions = {{content.x, content.y + 130, content.w, 32},
                   kPauseActionCount, 2};
    out.videoRows = {{content.x, content.y + 53, content.w, 56},
                     kPauseVideoRowCount, 8};
    out.optionRows = {{content.x, content.y + 53, content.w, 40},
                      kPauseCoreOptionRows, 5};
    // Sits a little below the optical centre, exactly as the fixed box did.
    out.power =
        confirmLayout({(width - 544) / 2, height / 2 - 104, 544, 224}, 136, 38,
                      19, 16);
    // The card floats over the dimmed picture in the gutter left of the drawer.
    out.thumbnail = {std::max(24.0f, bounds.x - 240), 221, 214, 248};
    // The toast centres on that same gutter instead of running under the panel.
    const float toastWidth = std::min(496.0f, std::max(0.0f, bounds.x - 96));
    out.toast = {(bounds.x - toastWidth) / 2, height - 112, toastWidth, 46};
    return out;
  }();
  return layout;
}

void drawSidebar(SDL_Renderer *renderer, const PauseLayout &layout,
                 const std::string &subtitle, i18n::Language language,
                 SDL_Color accent) {
  const PanelLayout &panel = layout.panel;
  fill(renderer, 0, 0, kCanvasWidth, kCanvasHeight, {2, 5, 12, 64});
  drawPanel(renderer, panel.bounds, 28, {12, 21, 37, 225},
            panel.bounds.expanded(8, 0).translated(0, 8), {0, 0, 0, 45});
  const Rect header = panel.header;
  rounded(renderer, header.x, header.y, 4, 32, 2, accent);
  text(renderer, header.x + 20, header.y + 2,
       tr(TextId::PauseTitle, language), 2, accent);
  text(renderer, header.x, header.y + 54,
       fitText(subtitle, static_cast<int>(header.w), 3), 3,
       {245, 247, 252, 255});
  fill(renderer, header.x, header.bottom(), header.w, 1,
       {255, 255, 255, 30});
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
  const PauseLayout &layout = pauseLayout();
  const PanelLayout &panel = layout.panel;
  drawSidebar(renderer, layout, std::string(gameTitle), language, accent);
  text(renderer, panel.content.x, panel.content.y + 5,
       tr(TextId::StateSlot, language), 1, {174, 188, 208, 255});
  for (int slot = 0; slot < kStateSlotCount; ++slot) {
    const Rect bounds = layout.slots.item(slot);
    const float x = bounds.x;
    const bool selected = slot == stateSlot;
    rounded(renderer, bounds.x, bounds.y, bounds.w, bounds.h, 12,
            selected ? accent
                     : SDL_Color{255, 255, 255, 18});
    centeredText(
        renderer, static_cast<int>(x + bounds.w / 2),
        static_cast<int>(bounds.y + 12), std::to_string(slot + 1), 2,
        selected ? SDL_Color{23, 23, 38, 255} : SDL_Color{230, 235, 245, 255});
  }
  circle(renderer, static_cast<int>(panel.content.x + 4),
         static_cast<int>(panel.content.y + 106), 4,
         current.present ? SDL_Color{132, 220, 157, 255}
                         : SDL_Color{142, 155, 179, 255});
  text(renderer, static_cast<int>(panel.content.x + 16),
       static_cast<int>(panel.content.y + 97),
       tr(current.present ? TextId::StateAvailable : TextId::StateEmpty,
          language),
       1, {184, 204, 212, 255});
  // The frame-step legend names the action first, then the binding; it sits on
  // the slot heading row so it never crowds the action list.
  drawHintRow(renderer, panel.content.right(), panel.content.y,
              {{tr(TextId::FrameStep, language), prompts.frameStep, false}},
              accent);

  // Thumbnail card for the focused slot, parked over the dimmed game picture.
  if (current.present) {
    const float kCardX = layout.thumbnail.x;
    const float kCardY = layout.thumbnail.y;
    const float kCardWidth = layout.thumbnail.w;
    const float kCardHeight = layout.thumbnail.h;
    rounded(renderer, kCardX, kCardY, kCardWidth, kCardHeight, 20,
            {8, 16, 31, 245});
    rounded(renderer, kCardX + 6, kCardY + 6, kCardWidth - 12, 192, 14,
            {3, 7, 18, 255});
    if (current.thumbnail != nullptr) {
      drawTexture(renderer, current.thumbnail, kCardX + 6.0f, kCardY + 6.0f,
                  205.0f, 192.0f);
    }
    if (!current.savedAt.empty()) {
      text(renderer, static_cast<int>(kCardX + 10.0f),
           static_cast<int>(kCardY + 205.0f),
           std::string(tr(TextId::StateSavedAt, language)) + " " +
               current.savedAt,
           1,
           {184, 204, 212, 255});
    }
    if (!current.playTime.empty()) {
      text(renderer, static_cast<int>(kCardX + 10.0f),
           static_cast<int>(kCardY + 224.0f), current.playTime, 1,
           accent);
    }
  }
  const std::array<TextId, kPauseActionCount> labels{
      TextId::ResumeGame,   TextId::SaveState,    TextId::LoadState,
      TextId::ConsoleReset, TextId::PowerCycle,   TextId::Display,
      TextId::CoreOptions,  TextId::ControlHelp,  TextId::ReturnHome};
  // No key glyphs here. Nothing in this menu is a keyboard shortcut -- every
  // row is picked with the d-pad and confirmed -- and the handheld build has no
  // keyboard at all, so the circle letters only promised keys that are not
  // there. The footer keeps the two prompts that do exist on the device.
  for (int action = 0; action < kPauseActionCount; ++action) {
    const Rect bounds = layout.actions.item(action);
    const bool selected = action == selectedAction;
    drawFocusablePlate(renderer, bounds.x, bounds.y, bounds.w, bounds.h, 16,
                       selected ? accent : SDL_Color{255, 255, 255, 16},
                       selected);
    text(renderer, static_cast<int>(panel.content.x + 20),
         textCenteredY(tr(labels[action], language),
                       static_cast<int>(bounds.centerY()), 2),
         tr(labels[action], language), 2,
         selected ? SDL_Color{24, 24, 39, 255} : SDL_Color{255, 255, 255, 255});
  }

  // No key legend under the list: CONTINUE and RETURN HOME are rows of their
  // own, so a strip naming the same two keys would repeat them.

  if (SDL_GetTicks64() < feedbackUntil && !feedback.empty()) {
    const Rect &toast = layout.toast;
    rounded(renderer, toast.x, toast.y, toast.w, toast.h, 14, {3, 8, 20, 245});
    centeredText(renderer, static_cast<int>(toast.centerX()),
                 static_cast<int>(toast.y + 13), std::string(feedback), 2,
                 accent);
  }
}

int pauseSlotAt(float x, float y) {
  return pauseLayout().slots.hit(x, y);
}

void drawPowerCycleConfirm(SDL_Renderer *renderer, int choice,
                           i18n::Language language, SDL_Color accent) {
  const ConfirmLayout &power = pauseLayout().power;
  drawConfirmFrame(renderer, power, tr(TextId::PowerCycleTitle, language),
                   tr(TextId::PowerCycleBody, language), accent, 150);
  drawChoiceButtons(renderer, power.buttons,
                    {tr(TextId::Cancel, language),
                     tr(TextId::PowerCycle, language)}, choice, accent);
  // Cancel on the left, confirm on the right, no keys underneath: the buttons
  // carry the words, so a legend would only repeat them.
}

int powerCycleChoiceAt(float x, float y) {
  return pauseLayout().power.buttons.hit(x, y);
}

int pauseVideoRowAt(float x, float y) {
  return pauseLayout().videoRows.hit(x, y);
}

void drawPauseVideoPanel(SDL_Renderer *renderer, int row,
                         const PauseVideoState &video, i18n::Language language,
                         SDL_Color accent) {
  const PauseLayout &layout = pauseLayout();
  const PanelLayout &panel = layout.panel;
  drawSidebar(renderer, layout, tr(TextId::GameDisplayTitle, language),
              language, accent);

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
    const Rect bounds = layout.videoRows.item(index);
    const float y = bounds.y;
    const bool selected = index == row;
    drawFocusablePlate(renderer, bounds.x, bounds.y, bounds.w, bounds.h,
                       14,
                       selected ? accent : SDL_Color{255, 255, 255, 16},
                       selected);
    text(renderer, static_cast<int>(panel.content.x + 16),
         static_cast<int>(y + 9), titles[index], 2,
         selected ? SDL_Color{24, 24, 39, 255}
                  : SDL_Color{241, 245, 251, 255});
    text(renderer, static_cast<int>(panel.content.right() - 16) -
                       textWidth(values[index], 2),
         static_cast<int>(y + 12), values[index], 2,
         selected ? SDL_Color{24, 24, 39, 255}
                  : accent);
    if (index + 1 == kPauseVideoRowCount) {
      text(renderer, static_cast<int>(panel.content.x + 16),
           static_cast<int>(y + 34),
           tr(TextId::UseGlobalDefaultsDescription, language), 1,
           selected ? SDL_Color{70, 57, 39, 255}
                    : SDL_Color{165, 181, 204, 255});
    }
  }

  const float hintY =
      layout.videoRows.item(kPauseVideoRowCount - 1).bottom() + 26;
  text(renderer, static_cast<int>(panel.content.x), static_cast<int>(hintY),
       fitText(tr(TextId::UseGlobalDefaultsHint, language),
               static_cast<int>(panel.content.w), 1),
       1, {165, 181, 204, 255});

}

int pauseActionAt(float x, float y) {
  return pauseLayout().actions.hit(x, y);
}

int pauseCoreOptionAt(float x, float y, int scroll) {
  const int row = pauseLayout().optionRows.hit(x, y);
  return row < 0 ? -1 : scroll + row;
}

void drawPauseCoreOptionsPanel(SDL_Renderer *renderer, int cursor, int scroll,
                               const runtime::CoreOptionList &options,
                               i18n::Language language, SDL_Color accent) {
  const PauseLayout &layout = pauseLayout();
  const PanelLayout &panel = layout.panel;
  drawSidebar(renderer, layout, tr(TextId::CoreOptions, language), language,
              accent);

  const int total = static_cast<int>(options.size());
  if (total == 0) {
    text(renderer, static_cast<int>(panel.content.x),
         static_cast<int>(panel.content.y + 69),
         fitText(tr(TextId::CoreOptionsEmpty, language),
                 static_cast<int>(panel.content.w), 2),
         2, {165, 181, 204, 255});
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
      const Rect bounds = layout.optionRows.item(row);
      const float y = bounds.y;
      drawFocusablePlate(renderer, bounds.x, bounds.y, bounds.w, bounds.h, 14,
                         selected ? accent : SDL_Color{255, 255, 255, 16},
                         selected);
      const std::string label =
          i18n::coreOptionLabel(option.key, option.label, language);
      text(renderer, static_cast<int>(panel.content.x + 16),
            static_cast<int>(y + 10),
            fitText(label, static_cast<int>(panel.content.w / 2), 2), 2,
           selected ? SDL_Color{24, 24, 39, 255}
                    : SDL_Color{241, 245, 251, 255});
      std::string value;
      if (option.selected < option.values.size()) {
        const runtime::CoreOptionValue &entry = option.values[option.selected];
        value = i18n::coreOptionValue(entry.value, entry.label, language);
      }
      value = fitText(value, static_cast<int>(panel.content.w * 19 / 48), 2);
      text(renderer, static_cast<int>(panel.content.right() - 16) -
                         textWidth(value, 2),
           static_cast<int>(y + 10), value, 2,
           selected ? SDL_Color{24, 24, 39, 255}
                    : accent);
    }
    const float rowsBottom =
        layout.optionRows.item(kPauseCoreOptionRows - 1).bottom();
    text(renderer, static_cast<int>(panel.content.x),
         static_cast<int>(rowsBottom + 23),
         std::to_string(cursor + 1) + " / " + std::to_string(total), 1,
         {165, 181, 204, 255});
  }
}

} // namespace launcher::ui
