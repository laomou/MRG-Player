#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "app/LaunchOptions.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string &what) {
  if (condition)
    return;
  std::fprintf(stderr, "FAIL: %s\n", what.c_str());
  ++failures;
}

// argv stays mutable so the parser sees exactly what main() hands it.
launcher::LaunchOptions parse(std::vector<std::string> arguments) {
  arguments.insert(arguments.begin(), "mrg-player");
  std::vector<char *> argv;
  argv.reserve(arguments.size());
  for (std::string &argument : arguments)
    argv.push_back(argument.data());
  return launcher::parseLaunchOptions(static_cast<int>(argv.size()),
                                      argv.data());
}

} // namespace

int main() {
  {
    const launcher::LaunchOptions options = parse({});
    check(options.error.empty(), "no arguments is not an error");
    check(!options.directGame, "no ROM means the library opens");
    check(options.romPath.empty(), "no ROM path without a flag");
  }

  // The desktop hands an emulator the ROM the player picked, so both spellings
  // of the flag have to work, and a bare path is the shape a launch script uses.
  std::vector<std::vector<std::string>> romArguments{
      {"--rom=/roms/FC/game.nes"},
      {"--rom", "/roms/FC/game.nes"},
      {"/roms/FC/game.nes"}};
  for (const std::vector<std::string> &arguments : romArguments) {
    const launcher::LaunchOptions options = parse(arguments);
    check(options.error.empty(), "a ROM path is accepted");
    check(options.directGame, "a ROM path starts in the game");
    check(options.romPath == "/roms/FC/game.nes", "the ROM path is kept");
    check(!options.smokeTest(), "a ROM path is not a scripted run");
  }

  {
    const launcher::LaunchOptions options = parse({"--rom"});
    check(!options.error.empty(), "a bare --rom reports the missing path");
    check(!options.directGame, "a missing path does not start a game");
  }

  {
    const launcher::LaunchOptions options =
        parse({"--language=en", "--rom=/roms/FC/game.nes"});
    check(options.error.empty(), "language and ROM combine");
    check(options.languageOverride == 0, "the language override survives");
    check(options.directGame, "the ROM still starts the game");
  }

  {
    const launcher::LaunchOptions options = parse({"a.nes", "b.nes"});
    check(!options.error.empty(), "a second path is rejected");
  }

  {
    const launcher::LaunchOptions options = parse({"--wat"});
    check(!options.error.empty(), "an unknown flag is rejected");
    check(!options.directGame, "an unknown flag does not start a game");
  }

  if (failures > 0) {
    std::fprintf(stderr, "%d launch-option check(s) failed\n", failures);
    return 1;
  }
  std::fprintf(stdout, "launch-option checks passed\n");
  return 0;
}
