#include "platform/BrickLed.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>

namespace launcher::platform {
namespace {

constexpr const char *kRoot = "/sys/class/led_anim/";
constexpr const char *kZones[] = {"lr", "m", "f1", "f2"};

std::string pathFor(const std::string &name) {
  return std::string(kRoot) + name;
}

std::string readValue(const std::string &path) {
  std::ifstream input(path);
  if (!input)
    return {};
  std::string value;
  std::getline(input, value);
  return value;
}

void writeValue(const std::string &path, const std::string &value) {
  std::ofstream output(path, std::ios::trunc);
  if (output)
    output << value << '\n';
}

std::string hexColour(std::uint32_t rgb) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%06X", rgb & 0xFFFFFFu);
  return buffer;
}

} // namespace

bool BrickLeds::available() { return std::ifstream(pathFor("enable")).good(); }

void BrickLeds::capture() {
  if (captured_ || !available())
    return;
  const char *names[] = {"enable", "effect_enable", "effect_rgb_hex_lr",
                         "effect_rgb_hex_m", "effect_rgb_hex_f1",
                         "effect_rgb_hex_f2"};
  for (std::size_t index = 0; index < saved_.size(); ++index) {
    saved_[index].path = pathFor(names[index]);
    saved_[index].value = readValue(saved_[index].path);
  }
  captured_ = true;
}

void BrickLeds::apply(std::uint32_t rgb) {
  if (!available())
    return;
  const std::string colour = hexColour(rgb);
  writeValue(pathFor("enable"), "1");
  for (const char *zone : kZones)
    writeValue(pathFor(std::string("effect_rgb_hex_") + zone), colour);
  // A single short pulse marks the change without turning the case into a
  // beacon while a game is running.
  writeValue(pathFor("effect_enable"), "1");
  for (const char *zone : kZones) {
    writeValue(pathFor(std::string("effect_cycles_") + zone), "1");
    writeValue(pathFor(std::string("effect_duration_") + zone), "600");
    writeValue(pathFor(std::string("effect_") + zone), "1");
  }
}

void BrickLeds::restore() {
  if (!captured_)
    return;
  for (const Entry &entry : saved_)
    if (!entry.path.empty() && !entry.value.empty())
      writeValue(entry.path, entry.value);
  captured_ = false;
}

} // namespace launcher::platform
