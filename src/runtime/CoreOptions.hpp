#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace launcher::runtime {

// One selectable value of a core option. The label is what the pause menu
// shows; the value is the exact string the core expects back.
struct CoreOptionValue {
  std::string label;
  std::string value;
};

// One setting a libretro core declares through the environment callback.
struct CoreOption {
  std::string key;
  std::string label;
  std::string info;
  std::string category;
  std::vector<CoreOptionValue> values;
  // What the core declared as its default, and what the player has picked.
  std::size_t defaultSelected = 0;
  std::size_t selected = 0;
};

using CoreOptionList = std::vector<CoreOption>;

} // namespace launcher::runtime
