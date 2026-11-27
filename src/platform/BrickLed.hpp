#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace launcher::platform {

// The Brick drives its case lighting through /sys/class/led_anim: one colour
// per zone (left/right strips, middle, the two function keys) plus a trigger
// that flashes that zone. Firmware and the stock launcher write the same
// files, so whatever we change has to be put back when we leave.
class BrickLeds {
public:
  [[nodiscard]] static bool available();

  // Remembers the current values once; calling it again is a no-op.
  void capture();

  // Paints every zone with the colour and triggers a short pulse. Ignored when
  // the class is missing, so a desktop build can call this freely.
  void apply(std::uint32_t rgb);

  // Puts the remembered values back.
  void restore();

private:
  struct Entry {
    std::string path;
    std::string value;
  };
  std::array<Entry, 6> saved_{};
  bool captured_ = false;
};

} // namespace launcher::platform
