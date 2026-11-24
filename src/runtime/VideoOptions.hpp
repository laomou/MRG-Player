#pragma once

namespace launcher::runtime {

// How the console picture is fitted into the window. NES pixels were not
// square: the 256x240 buffer was displayed either on a full 4:3 tube or with
// its own 8:7 pixel aspect ratio.
enum class AspectMode { Pixel = 0, Screen4x3 = 1, Pixels8x7 = 2 };

// Largest over-scan crop a runtime has to support, in pixels per edge.
inline constexpr int kMaxOverscan = 16;

struct VideoOptions {
  AspectMode aspect = AspectMode::Pixel;
  bool integerScale = false;
  // Pixels hidden on every edge, the way a CRT over-scans the picture.
  int overscan = 0;
};

} // namespace launcher::runtime
