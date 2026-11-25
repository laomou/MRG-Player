#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace launcher::ui {

// Canvas-space geometry shared by painting, focus rings and pointer targets.
// Half-open edges keep neighbouring controls from sharing a hit pixel.
struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  constexpr float right() const { return x + w; }
  constexpr float bottom() const { return y + h; }
  constexpr float centerX() const { return x + w / 2; }
  constexpr float centerY() const { return y + h / 2; }
  constexpr bool contains(float px, float py) const {
    return w > 0 && h > 0 && px >= x && px < right() && py >= y && py < bottom();
  }
  constexpr Rect translated(float dx, float dy) const {
    return {x + dx, y + dy, w, h};
  }
  constexpr Rect expanded(float dx, float dy) const {
    return {x - dx, y - dy, w + 2 * dx, h + 2 * dy};
  }
  Rect inset(float left, float top, float right, float bottom) const {
    return {x + left, y + top, std::max(0.0f, w - left - right),
            std::max(0.0f, h - top - bottom)};
  }
};

enum class Axis { Vertical, Horizontal };

// Regular lists can carry one section break (e.g. destructive actions).
struct LinearLayout {
  Rect first;
  int count = 0;
  float gap = 0;
  Axis axis = Axis::Vertical;
  int breakBefore = -1;
  float sectionGap = 0;

  Rect item(int index) const {
    if (index < 0 || index >= count)
      return {};
    const float step = axis == Axis::Vertical ? first.h : first.w;
    const float offset = index * (step + gap) +
        (breakBefore >= 0 && index >= breakBefore ? sectionGap : 0);
    return axis == Axis::Vertical ? first.translated(0, offset)
                                  : first.translated(offset, 0);
  }
  int hit(float x, float y) const {
    for (int index = 0; index < count; ++index)
      if (item(index).contains(x, y))
        return index;
    return -1;
  }
};

inline LinearLayout equalColumns(Rect area, int count, float gap) {
  if (count <= 0)
    return {};
  const float width = std::max(0.0f, (area.w - (count - 1) * gap) / count);
  return {{area.x, area.y, width, area.h}, count, gap, Axis::Horizontal};
}

// Right-aligned values may use at most half the row; long values must not
// push their labels outside the control. Text fitting happens in the renderer.
struct LabelValueLayout {
  Rect label, value;
};

inline LabelValueLayout labelValueLayout(Rect area, float valueWidth,
                                        float gap = 16) {
  const float available = std::max(0.0f, area.w - gap);
  const float width = std::clamp(valueWidth, 0.0f, available / 2);
  return {{area.x, area.y, available - width, area.h},
          {area.right() - width, area.y, width, area.h}};
}

// A panel's header, content and footer are reserved before children are laid out.
struct PanelLayout {
  Rect bounds, header, content, footer;
};

inline PanelLayout panelLayout(Rect bounds, Rect inner, float headerHeight,
                               float footerHeight, float gap) {
  const float header = std::clamp(headerHeight, 0.0f, inner.h);
  const float footer = std::clamp(footerHeight, 0.0f, inner.h - header);
  return {bounds, {inner.x, inner.y, inner.w, header},
          {inner.x, inner.y + header + gap, inner.w,
           std::max(0.0f, inner.h - header - footer - 2 * gap)},
          {inner.x, inner.bottom() - footer, inner.w, footer}};
}

struct ConfirmLayout {
  Rect bounds;
  Rect heading;
  Rect body;
  LinearLayout buttons;
  Rect footer;
};

// Button widths come from the row, never from separate paint/hit constants.
inline ConfirmLayout confirmLayout(Rect bounds, float buttonTop,
                                   float buttonInset, float buttonGap,
                                   float footerBottom) {
  const Rect inner = bounds.inset(36, 40, 36, 0);
  return {bounds, {inner.x, inner.y, inner.w, 32},
          {inner.x, inner.y + 66, inner.w, buttonTop - 114},
          equalColumns({bounds.x + buttonInset, bounds.y + buttonTop,
                        bounds.w - 2 * buttonInset, 64}, 2, buttonGap),
          {inner.x, bounds.bottom() - footerBottom - 26, inner.w, 26}};
}

} // namespace launcher::ui
