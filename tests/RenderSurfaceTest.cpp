#include "app/RenderSurface.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void check(bool result, const char *message) {
  if (!result) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

bool same(launcher::OutputSize size, int width, int height) {
  return size.width == width && size.height == height;
}

} // namespace

int main() {
  using launcher::parseOutputSizeOverride;
  using launcher::resolveOutputSize;

  // The handheld panel is 1024x768; both separators and surrounding spaces work.
  check(same(parseOutputSizeOverride("1024x768"), 1024, 768),
        "Lower-case separator parses");
  check(same(parseOutputSizeOverride("1024X768"), 1024, 768),
        "Upper-case separator parses");
  check(same(parseOutputSizeOverride("1280*720"), 1280, 720),
        "Star separator parses");
  check(same(parseOutputSizeOverride("  1024x768  "), 1024, 768),
        "Surrounding spaces are ignored");
  check(same(parseOutputSizeOverride(" 1024 x 768 "), 1024, 768),
        "Spaces around the numbers are ignored");

  // A typo must not leave the launcher with an empty framebuffer.
  check(!parseOutputSizeOverride("").valid(), "Empty text is rejected");
  check(!parseOutputSizeOverride("1024").valid(), "A single number is rejected");
  check(!parseOutputSizeOverride("1024x").valid(), "A missing height is rejected");
  check(!parseOutputSizeOverride("x768").valid(), "A missing width is rejected");
  check(!parseOutputSizeOverride("0x768").valid(), "Zero width is rejected");
  check(!parseOutputSizeOverride("1024x0").valid(), "Zero height is rejected");
  check(!parseOutputSizeOverride("-1024x768").valid(), "Negative width is rejected");
  check(!parseOutputSizeOverride("1024x-768").valid(), "Negative height is rejected");
  check(!parseOutputSizeOverride("1024x768junk").valid(), "Trailing junk is rejected");
  check(!parseOutputSizeOverride("junkx768").valid(), "Leading junk is rejected");
  check(!parseOutputSizeOverride("8193x768").valid(), "Oversized width is rejected");
  check(!parseOutputSizeOverride("1024x8193").valid(), "Oversized height is rejected");
  check(!parseOutputSizeOverride("99999999999x768").valid(),
        "Overflowing digits are rejected");
  check(same(parseOutputSizeOverride("8192x8192"), 8192, 8192),
        "The maximum accepted size parses");

  // Override beats the renderer, which beats the window, which beats nothing.
  check(same(resolveOutputSize(1280, 720, 1024, 768, "1024x768"), 1024, 768),
        "An explicit override wins over the reported size");
  check(same(resolveOutputSize(1280, 720, 1024, 768, "bogus"), 1280, 720),
        "A malformed override falls back to the reported size");
  check(same(resolveOutputSize(1280, 720, 1024, 768, ""), 1280, 720),
        "An empty override falls back to the reported size");
  check(same(resolveOutputSize(1280, 720, 1024, 768, nullptr), 1280, 720),
        "No override at all still uses the reported size");
  check(same(resolveOutputSize(0, 0, 1024, 768, nullptr), 1024, 768),
        "An unusable report falls back to the window size");
  check(same(resolveOutputSize(-1, 720, 1024, 768, nullptr), 1024, 768),
        "Half a report is not trusted");
  check(!resolveOutputSize(0, 0, 0, 0, nullptr).valid(),
        "No usable size leaves the UI untouched");

  std::cout << "Render surface resolution passed\n";
  return 0;
}
