#include "runtime/VideoViewport.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
constexpr int kWidth = 256;
constexpr int kHeight = 240;

void check(bool result, const char *message) {
  if (!result) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

bool nearly(float a, float b) { return std::fabs(a - b) < 0.01f; }
} // namespace

int main() {
  using launcher::runtime::AspectMode;
  using launcher::runtime::computeViewport;
  using launcher::runtime::VideoOptions;

  // Square pixels in a 1600x900 window: height-limited, centred horizontally.
  VideoOptions square;
  auto view = computeViewport(kWidth, kHeight, 1600, 900, square);
  check(view.sourceX == 0 && view.sourceY == 0, "No crop by default");
  check(view.sourceWidth == kWidth && view.sourceHeight == kHeight,
        "Source keeps the raw framebuffer");
  check(nearly(view.height, 900.0f) && nearly(view.width, 960.0f),
        "Square pixels fill the height");
  check(nearly(view.x, 320.0f) && nearly(view.y, 0.0f),
        "Letterboxing is centred");

  // 4:3 stretches the same buffer to the shape of a television.
  VideoOptions screen;
  screen.aspect = AspectMode::Screen4x3;
  view = computeViewport(kWidth, kHeight, 1600, 900, screen);
  check(nearly(view.width / view.height, 4.0f / 3.0f),
        "4:3 mode must produce a 4:3 picture");
  check(nearly(view.height, 900.0f) && nearly(view.x, 200.0f),
        "4:3 stays height-limited and centred");

  // 8:7 is wider than square pixels but narrower than 4:3.
  VideoOptions eightSeven;
  eightSeven.aspect = AspectMode::Pixels8x7;
  view = computeViewport(kWidth, kHeight, 1600, 900, eightSeven);
  check(nearly(view.width, kWidth * (8.0f / 7.0f) * (900.0f / kHeight)),
        "8:7 pixels scale horizontally");
  check(nearly(view.width / view.height, (256.0f * 8.0f / 7.0f) / 240.0f),
        "8:7 ratio is not 4:3");

  // Integer scaling snaps to whole multiples and leaves a border.
  VideoOptions integer = square;
  integer.integerScale = true;
  view = computeViewport(kWidth, kHeight, 1600, 900, integer);
  check(nearly(view.width, 768.0f) && nearly(view.height, 720.0f),
        "Integer scaling uses the floor of the available factor");
  check(nearly(view.x, 416.0f) && nearly(view.y, 90.0f),
        "Integer scaling stays centred");

  // A window smaller than the source still scales down instead of collapsing.
  view = computeViewport(kWidth, kHeight, 200, 150, integer);
  check(nearly(view.width, 160.0f) && nearly(view.height, 150.0f),
        "Integer scaling falls back to fractional scaling when tiny");

  // Over-scan crops the source and keeps the picture centred.
  VideoOptions cropped = square;
  cropped.overscan = 8;
  view = computeViewport(kWidth, kHeight, 1600, 900, cropped);
  check(view.sourceX == 8 && view.sourceY == 8, "Overscan crops every edge");
  check(view.sourceWidth == kWidth - 16 && view.sourceHeight == kHeight - 16,
        "Overscan shrinks the source rectangle");
  check(nearly(view.width, 240.0f * (900.0f / 224.0f)),
        "Overscan keeps the picture height-limited");

  // Out-of-range options are clamped instead of producing nonsense.
  VideoOptions crazy = square;
  crazy.overscan = 4096;
  view = computeViewport(kWidth, kHeight, 1600, 900, crazy);
  check(view.sourceWidth == kWidth - launcher::runtime::kMaxOverscan * 2,
        "Overscan clamps to the documented maximum");

  // Degenerate windows are rejected rather than dividing by zero.
  view = computeViewport(kWidth, kHeight, 0, 0, square);
  check(view.sourceWidth == 0 && view.width == 0.0f,
        "A zero-sized window produces an empty viewport");

  std::cout << "Display viewport geometry passed\n";
  return 0;
}
