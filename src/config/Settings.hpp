#pragma once

#include <string>

#include "config/InputBindings.hpp"

namespace launcher {

struct AppSettings {
  int language = 0;
  int startupPage = 0;
  bool rememberSelection = true;
  bool fullscreen = false;
  int volume = 80;
  // Muting hides the output without losing the volume above; the toggle also
  // has a hotkey during play.
  bool audioMuted = false;
  // What fast-forward does with sound: 0 = silence, 1 = keep it audible.
  int fastForwardAudio = 0;
  // Console picture fitting: 0 = square pixels, 1 = 4:3 screen, 2 = 8:7.
  int videoAspect = 0;
  bool videoIntegerScale = false;
  // Pixels cropped on every edge, in the 0..16 range.
  int videoOverscan = 0;
  // Rewind keeps a rolling window of machine snapshots behind the playhead so
  // the player can scrub back over a mistake.
  bool rewind = true;
  // Frames the picture is allowed to run ahead of the input, 0 to 3.
  int runahead = 0;
  // Accent colour shared by every screen, 0 to 5 (ui::kAccentCount).
  int accent = 0;
  int selectedGame = 2;
  config::PromptDeviceMode promptDevice = config::PromptDeviceMode::Auto;
  config::ControllerStyle controllerStyle = config::ControllerStyle::Auto;
  std::string preferredControllerGuid;
  std::string preferredControllerGuid2;
  // Which player the settings screen is currently editing (0 or 1).
  int bindingsPlayer = 0;
  config::InputBindings input = config::defaultInputBindings();
};

std::string settingsPath();
void loadSettings(AppSettings &settings);
void saveSettings(const AppSettings &settings);

} // namespace launcher
