#pragma once

#include <SDL.h>

#include <cstddef>

// One accent for the whole launcher. A console theme paints "what is selected"
// and "what the keys act on" in a single hue, and lets box art carry the
// identity of individual games, so this table is the only place a colour is
// chosen.
namespace launcher::ui {

inline constexpr int kAccentCount = 6;

inline constexpr SDL_Color kAccentPalette[kAccentCount] = {
    {86, 223, 202, 255},  // Teal, the logo colour and the default.
    {95, 181, 255, 255},  // Blue.
    {181, 135, 255, 255}, // Purple.
    {153, 224, 92, 255},  // Lime.
    {255, 136, 92, 255},  // Orange.
    {255, 211, 89, 255},  // Yellow.
};

[[nodiscard]] inline SDL_Color accentColor(int index) noexcept {
  const int wrapped = ((index % kAccentCount) + kAccentCount) % kAccentCount;
  return kAccentPalette[static_cast<std::size_t>(wrapped)];
}

} // namespace launcher::ui
