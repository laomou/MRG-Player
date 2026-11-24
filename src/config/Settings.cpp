#include "config/Settings.hpp"

#include "config/AppPaths.hpp"
#include "util/Utf8Path.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace launcher {
namespace {

constexpr std::array<const char *, config::kInputActionCount> kKeyNames{
    "key_a",  "key_b",    "key_select", "key_start",  "key_up",
    "key_down", "key_left", "key_right", "key_turbo_a", "key_turbo_b"};
constexpr std::array<const char *, config::kInputActionCount> kPadNames{
    "pad_a",  "pad_b",    "pad_select", "pad_start",  "pad_up",
    "pad_down", "pad_left", "pad_right", "pad_turbo_a", "pad_turbo_b"};
constexpr std::array<const char *, config::kInputActionCount> kKey2Names{
    "key2_a",  "key2_b",    "key2_select", "key2_start",  "key2_up",
    "key2_down", "key2_left", "key2_right", "key2_turbo_a", "key2_turbo_b"};
constexpr std::array<const char *, config::kInputActionCount> kPad2Names{
    "pad2_a",  "pad2_b",    "pad2_select", "pad2_start",  "pad2_up",
    "pad2_down", "pad2_left", "pad2_right", "pad2_turbo_a", "pad2_turbo_b"};

} // namespace

std::string settingsPath() {
  std::string migrationError;
  (void)config::migrateLegacyData(migrationError);
  const std::string root = config::dataRoot();
  return root.empty() ? "mrg-player.ini"
                      : paths::toUtf8(paths::fromUtf8(root) / "settings.ini");
}

void loadSettings(AppSettings &settings) {
  std::ifstream input(paths::fromUtf8(settingsPath()));
  std::string line;
  while (std::getline(input, line)) {
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos)
      continue;
    const std::string key = line.substr(0, separator);
    const std::string rawValue = line.substr(separator + 1);
    const int value = std::atoi(rawValue.c_str());
    if (key == "language")
      settings.language = value == 2 ? 1 : std::clamp(value, 0, 1);
    else if (key == "startup_page")
      settings.startupPage = std::clamp(value, 0, 1);
    else if (key == "remember_selection")
      settings.rememberSelection = value != 0;
    else if (key == "fullscreen")
      settings.fullscreen = value != 0;
    else if (key == "volume")
      settings.volume = std::clamp(value, 0, 100);
    else if (key == "audio_muted")
      settings.audioMuted = value != 0;
    else if (key == "fast_forward_audio")
      settings.fastForwardAudio = std::clamp(value, 0, 1);
    else if (key == "video_aspect")
      settings.videoAspect = std::clamp(value, 0, 2);
    else if (key == "video_integer_scale")
      settings.videoIntegerScale = value != 0;
    else if (key == "video_overscan")
      settings.videoOverscan = std::clamp(value, 0, 16);
    else if (key == "rewind")
      settings.rewind = value != 0;
    else if (key == "runahead")
      settings.runahead = std::clamp(value, 0, 3);
    else if (key == "selected_game")
      settings.selectedGame = std::max(0, value);
    else if (key == "prompt_device")
      settings.promptDevice =
          static_cast<config::PromptDeviceMode>(std::clamp(value, 0, 2));
    else if (key == "controller_style")
      settings.controllerStyle =
          static_cast<config::ControllerStyle>(std::clamp(value, 0, 4));
    else if (key == "preferred_controller_guid")
      settings.preferredControllerGuid = rawValue;
    else if (key == "preferred_controller_guid2")
      settings.preferredControllerGuid2 = rawValue;
    else if (key == "bindings_player")
      settings.bindingsPlayer = std::clamp(value, 0, 1);
    else if (key == "controller_dead_zone")
      settings.input.analogDeadZone = std::clamp(value, 4000, 30000);
    else {
      for (std::size_t index = 0; index < config::kInputActionCount; ++index) {
        if (key == kKeyNames[index] && value >= 0)
          settings.input.keyboard[index] = static_cast<SDL_Keycode>(value);
        else if (key == kPadNames[index] && value >= 0 &&
                 value < SDL_CONTROLLER_BUTTON_MAX)
          settings.input.controller[index] =
              static_cast<SDL_GameControllerButton>(value);
        else if (key == kKey2Names[index] && value >= 0)
          settings.input.keyboardPlayer2[index] =
              static_cast<SDL_Keycode>(value);
        else if (key == kPad2Names[index] && value >= 0 &&
                 value < SDL_CONTROLLER_BUTTON_MAX)
          settings.input.controllerPlayer2[index] =
              static_cast<SDL_GameControllerButton>(value);
      }
    }
  }
}

void saveSettings(const AppSettings &settings) {
  std::ofstream output(paths::fromUtf8(settingsPath()), std::ios::trunc);
  if (!output)
    return;
  output << "language=" << settings.language << '\n'
         << "startup_page=" << settings.startupPage << '\n'
         << "remember_selection=" << (settings.rememberSelection ? 1 : 0)
         << '\n'
         << "fullscreen=" << (settings.fullscreen ? 1 : 0) << '\n'
         << "volume=" << settings.volume << '\n'
         << "audio_muted=" << (settings.audioMuted ? 1 : 0) << '\n'
         << "fast_forward_audio=" << settings.fastForwardAudio << '\n'
         << "video_aspect=" << settings.videoAspect << '\n'
         << "video_integer_scale=" << (settings.videoIntegerScale ? 1 : 0)
         << '\n'
         << "video_overscan=" << settings.videoOverscan << '\n'
         << "rewind=" << (settings.rewind ? 1 : 0) << '\n'
         << "runahead=" << settings.runahead << '\n'
         << "selected_game=" << settings.selectedGame << '\n'
         << "prompt_device=" << static_cast<int>(settings.promptDevice) << '\n'
         << "controller_style=" << static_cast<int>(settings.controllerStyle)
         << '\n'
         << "preferred_controller_guid=" << settings.preferredControllerGuid
         << '\n'
         << "preferred_controller_guid2=" << settings.preferredControllerGuid2
         << '\n'
         << "bindings_player=" << settings.bindingsPlayer << '\n'
         << "controller_dead_zone=" << settings.input.analogDeadZone << '\n';
  for (std::size_t index = 0; index < config::kInputActionCount; ++index) {
    output << kKeyNames[index] << '=' << settings.input.keyboard[index] << '\n'
           << kPadNames[index] << '='
           << static_cast<int>(settings.input.controller[index]) << '\n'
           << kKey2Names[index] << '=' << settings.input.keyboardPlayer2[index]
           << '\n'
           << kPad2Names[index] << '='
           << static_cast<int>(settings.input.controllerPlayer2[index]) << '\n';
  }
}

} // namespace launcher
