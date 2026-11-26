#pragma once

// The interface is drawn on a fixed canvas in logical pixels: every screen
// places its widgets on it and the window is then letterboxed around it. A
// canvas that matches the panel is what keeps handheld pixels square - the same
// interface scaled by 0.8 turns its own text into mush, so a 4:3 panel gets a
// 4:3 canvas and the layouts arrange themselves inside it instead.
//
// The size is chosen per platform by CMake (desktop 1280x720, brick 1024x768)
// and can be overridden for a preview build with -DMRG_CANVAS_WIDTH=/-DMRG_CANVAS_HEIGHT=.
// The fallbacks keep a plain g++ compile of a single screen working.
#ifndef MRG_CANVAS_WIDTH
#define MRG_CANVAS_WIDTH 1280
#endif
#ifndef MRG_CANVAS_HEIGHT
#define MRG_CANVAS_HEIGHT 720
#endif

namespace launcher::ui {

inline constexpr int kCanvasWidth = MRG_CANVAS_WIDTH;
inline constexpr int kCanvasHeight = MRG_CANVAS_HEIGHT;

} // namespace launcher::ui
