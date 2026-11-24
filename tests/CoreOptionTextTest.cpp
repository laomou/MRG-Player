#include <cstdio>
#include <string>

#include "i18n/CoreOptionText.hpp"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (condition)
    return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

}  // namespace

int main() {
  using launcher::i18n::Language;
  using launcher::i18n::coreOptionLabel;
  using launcher::i18n::coreOptionValue;

  const Language zh = Language::SimplifiedChinese;
  const Language en = Language::English;

  check(coreOptionLabel("mesen_overclock", "Overclock", zh) == "超频",
        "A known option key is translated");
  check(coreOptionLabel("mesen_overclock", "Overclock", en) == "Overclock",
        "English keeps the core's label");
  check(coreOptionLabel("core_specific_key", "Unknown", zh) == "Unknown",
        "An unknown key falls back to the core's label");

  check(coreOptionValue("enabled", "enabled", zh) == "开启",
        "A known value is translated");
  check(coreOptionValue("Enabled", "Enabled", zh) == "开启",
        "Value lookup ignores case");
  check(coreOptionValue("Very High", "Very High", zh) == "很高",
        "A multi-word value is translated");
  check(coreOptionValue("White (blend)", "White (blend)", zh) == "白（混合）",
        "A blended colour keeps its colour");
  check(coreOptionValue("42", "42", zh) == "42",
        "Numbers pass through");
  check(coreOptionValue("TWB64 Pack", "TWB64 Pack", zh) == "TWB64 Pack",
        "Unknown values fall back to the core's label");
  check(coreOptionValue("enabled", "enabled", en) == "enabled",
        "English keeps the core's value label");

  if (failures == 0)
    std::printf("core option text checks passed\n");
  return failures == 0 ? 0 : 1;
}
