#pragma once

#include <algorithm>
#include <cmath>

#include "runtime/VideoOptions.hpp"
#include "ui/Canvas.hpp"

namespace launcher::runtime {

// The launcher draws its own interface on the canvas from ui/Canvas.hpp and
// letterboxes it into the window, so widgets and the console picture share one
// transform and one canvas size.

// Scale and offset that fit the canvas into a window, letterboxing the leftover
// edges. Free of SDL types, like the viewport maths below.
struct CanvasTransform {
  float scale = 1.0f;
  float offsetX = 0.0f;
  float offsetY = 0.0f;
};

[[nodiscard]] inline CanvasTransform fitCanvas(int windowWidth,
                                               int windowHeight) noexcept {
  CanvasTransform canvas;
  if (windowWidth <= 0 || windowHeight <= 0)
    return canvas;
  const float width = static_cast<float>(ui::kCanvasWidth);
  const float height = static_cast<float>(ui::kCanvasHeight);
  canvas.scale =
      std::min(static_cast<float>(windowWidth) / width,
               static_cast<float>(windowHeight) / height);
  canvas.offsetX =
      (static_cast<float>(windowWidth) - width * canvas.scale) * 0.5f;
  canvas.offsetY =
      (static_cast<float>(windowHeight) - height * canvas.scale) * 0.5f;
  return canvas;
}

// Destination rectangle and source crop that place a console picture inside a
// window. Deliberately free of SDL types so the geometry can be checked
// without a video device.
struct Viewport {
  int sourceX = 0;
  int sourceY = 0;
  int sourceWidth = 0;
  int sourceHeight = 0;
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// Fits a sourceWidth x sourceHeight picture into windowWidth x windowHeight.
// Over-scan crops the source, the aspect mode stretches horizontal pixels, and
// integer scaling snaps the result to whole multiples of the cropped source so
// no pixel is sampled at a different size than its neighbours.
[[nodiscard]] inline Viewport computeViewport(int sourceWidth, int sourceHeight,
                                              int windowWidth, int windowHeight,
                                              const VideoOptions &options) noexcept {
  Viewport view;
  if (sourceWidth <= 0 || sourceHeight <= 0 || windowWidth <= 0 ||
      windowHeight <= 0)
    return view;
  const int crop = std::clamp(options.overscan, 0, kMaxOverscan);
  const int usedWidth = sourceWidth - crop * 2;
  const int usedHeight = sourceHeight - crop * 2;
  if (usedWidth <= 0 || usedHeight <= 0)
    return view;
  float pixelWidth = 1.0f;
  switch (options.aspect) {
  case AspectMode::Screen4x3:
    pixelWidth = (4.0f / 3.0f) * (static_cast<float>(usedHeight) /
                                  static_cast<float>(usedWidth));
    break;
  case AspectMode::Pixels8x7:
    pixelWidth = 8.0f / 7.0f;
    break;
  case AspectMode::Pixel:
  default:
    break;
  }
  const float available =
      std::min(static_cast<float>(windowWidth) / (usedWidth * pixelWidth),
               static_cast<float>(windowHeight) / usedHeight);
  const float scale = options.integerScale && available >= 1.0f
                          ? std::floor(available)
                          : available;
  view.sourceX = crop;
  view.sourceY = crop;
  view.sourceWidth = usedWidth;
  view.sourceHeight = usedHeight;
  view.width = usedWidth * pixelWidth * scale;
  view.height = usedHeight * scale;
  view.x = (static_cast<float>(windowWidth) - view.width) * 0.5f;
  view.y = (static_cast<float>(windowHeight) - view.height) * 0.5f;
  return view;
}

} // namespace launcher::runtime
