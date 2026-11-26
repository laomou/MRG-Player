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
  // A ROM given with --rom=<path>, --rom <path> or as a bare path. The stock
  // launcher hands an emulator the ROM the player picked, so directGame also
  // returns to the desktop instead of opening the library the player never saw.
  std::string romPath;
  bool directGame = false;
  std::string error;

  [[nodiscard]] bool smokeTest() const noexcept {
    return smokeHome || smokeSettings || smokeManagement || smokePause ||
           smokeRom || smokeScreenshot;
  }
};

LaunchOptions parseLaunchOptions(int argc, char **argv);

} // namespace launcher
