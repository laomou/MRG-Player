#include "app/LaunchOptions.hpp"

#include <string>

#include "i18n/Localization.hpp"

namespace launcher {
namespace {

bool argumentValue(int &index, int argc, char **argv,
                   const std::string &argument, const char *name,
                   std::string &value) {
  const std::string prefix = std::string(name) + "=";
  if (argument.rfind(prefix, 0) == 0) {
    value = argument.substr(prefix.size());
    return true;
  }
  if (argument == name && index + 1 < argc) {
    const std::string next = argv[index + 1];
    if (next.rfind("--", 0) != 0) {
      value = argv[++index];
      return true;
    }
  }
  return false;
}

// A lone path is spelled like a value, not like a flag: an Emus package calls
// its emulator as `launch.sh "<rom>"`, so MRG has to accept that shape too.
bool isBarePath(const std::string &argument) {
  return argument.size() > 1 && argument[0] != '-';
}

} // namespace

LaunchOptions parseLaunchOptions(int argc, char **argv) {
  LaunchOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    const bool romOption =
        argument == "--rom" || argument.rfind("--rom=", 0) == 0;
    bool known = argument == "--language=en" || argument == "--language=zh" ||
                 romOption;

    // The scripted runs exist for CI only; a release build rejects their flags
    // like any other unknown option.
#if defined(MRG_ENABLE_SMOKE_TESTS)
    options.smokeHome = options.smokeHome || argument == "--smoke-test" ||
                        argument == "--smoke-test-home" ||
                        argument.rfind("--smoke-test=", 0) == 0 ||
                        argument.rfind("--smoke-test-home=", 0) == 0;
    options.smokeSettings =
        options.smokeSettings || argument == "--smoke-test-settings";
    const bool managementSmokeArgument =
        argument == "--smoke-test-management" ||
        argument.rfind("--smoke-test-management=", 0) == 0;
    if (managementSmokeArgument) {
      options.smokeManagement = true;
      (void)argumentValue(i, argc, argv, argument, "--smoke-test-management",
                          options.romPath);
    }

    const bool pauseSmokeArgument =
        argument == "--smoke-test-pause" ||
        argument.rfind("--smoke-test-pause=", 0) == 0;
    if (pauseSmokeArgument) {
      options.smokePause = true;
      (void)argumentValue(i, argc, argv, argument, "--smoke-test-pause",
                          options.romPath);
    }

    const bool screenshotSmokeArgument =
        argument == "--smoke-test-screenshot" ||
        argument.rfind("--smoke-test-screenshot=", 0) == 0;
    if (screenshotSmokeArgument) {
      options.smokePause = true;
      options.smokeScreenshot = true;
      (void)argumentValue(i, argc, argv, argument, "--smoke-test-screenshot",
                          options.romPath);
    }

    const bool romSmokeArgument = argument == "--smoke-test-rom" ||
                                  argument.rfind("--smoke-test-rom=", 0) == 0;
    if (romSmokeArgument) {
      options.smokeRom = true;
      (void)argumentValue(i, argc, argv, argument, "--smoke-test-rom",
                          options.romPath);
    }

    known = known || options.smokeHome || options.smokeSettings ||
            managementSmokeArgument || pauseSmokeArgument ||
            screenshotSmokeArgument || romSmokeArgument;
#endif

    if (argument == "--language=en") {
      options.languageOverride = static_cast<int>(i18n::Language::English);
    } else if (argument == "--language=zh") {
      options.languageOverride =
          static_cast<int>(i18n::Language::SimplifiedChinese);
    }

    if (romOption) {
      std::string value;
      if (argumentValue(i, argc, argv, argument, "--rom", value) &&
          !value.empty()) {
        options.romPath = value;
        options.directGame = true;
      } else if (options.error.empty()) {
        options.error = "--rom needs a ROM path";
      }
    } else if (isBarePath(argument) && options.romPath.empty()) {
      options.romPath = argument;
      options.directGame = true;
      known = true;
    } else if (isBarePath(argument) && options.error.empty()) {
      options.error = "Unexpected argument: " + argument;
    }

    if (!known && options.error.empty())
      options.error = "Unknown option: " + argument;
  }

  return options;
}

} // namespace launcher
