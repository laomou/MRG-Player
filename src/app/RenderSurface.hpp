#pragma once

#include <string_view>

namespace launcher {

struct OutputSize {
  int width = 0;
  int height = 0;

  [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0; }
  [[nodiscard]] bool operator==(const OutputSize &other) const noexcept {
    return width == other.width && height == other.height;
  }
};

// "1024x768" (either case, surrounding spaces allowed) -> {1024, 768}.
// Anything else, including zero, negative or implausible sizes, is rejected so
// a typo never leaves the launcher with a blank framebuffer.
OutputSize parseOutputSizeOverride(std::string_view value);

// The framebuffer the interface lays itself out in. The handheld's SDL mali
// driver hard-codes the window to 1280x720 on a 1024x768 panel, so the numbers
// the backend reports cannot be trusted there; an explicit override wins, and
// when the renderer reports nothing usable the window size stands in.
OutputSize resolveOutputSize(int reportedWidth, int reportedHeight,
                             int windowWidth, int windowHeight,
                             const char *overrideValue);

} // namespace launcher
