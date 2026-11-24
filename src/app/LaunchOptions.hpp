#pragma once

#include <string>

namespace launcher {

struct LaunchOptions {
  bool smokeHome = false;
  bool smokeSettings = false;
  bool smokeManagement = false;
  bool smokePause = false;
  bool smokeRom = false;
  bool smokeScreenshot = false;
  int languageOverride = -1;
  std::string romPath;
  std::string error;

  [[nodiscard]] bool smokeTest() const noexcept {
    return smokeHome || smokeSettings || smokeManagement || smokePause ||
           smokeRom || smokeScreenshot;
  }
};

LaunchOptions parseLaunchOptions(int argc, char **argv);

} // namespace launcher
