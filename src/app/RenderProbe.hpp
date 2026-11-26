#pragma once

#include <SDL.h>

#include <cstdio>

namespace launcher {

// Prints what the video layer actually gave us: the compiled render drivers,
// the backend in use, the sizes SDL reports for the window and the drawable,
// the GL strings when the backend is OpenGL, and the Linux framebuffer the
// handheld driver opens directly. The Brick Pro panel is 1024x768 while its
// SDL2 fork hard-codes 1280x720, so this report is how that gets settled.
void printRendererReport(SDL_Window *window, SDL_Renderer *renderer,
                         std::FILE *out);

// Average frame and present times, printed every few hundred frames. Off
// unless MRG_FRAME_STATS is set, because a release build should stay quiet.
class FrameStats {
public:
  FrameStats();

  void frame(Uint64 frameTicks, Uint64 presentTicks);

private:
  bool enabled_ = false;
  unsigned long long frames_ = 0;
  unsigned long long frameTicks_ = 0;
  unsigned long long presentTicks_ = 0;
};

} // namespace launcher
