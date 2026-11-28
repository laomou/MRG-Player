// The update check only needs one thing to be right: does a tag order above
// the running build? These are the shapes tags actually take.

#include <cstdio>
#include <string>
#include <string_view>

#include "util/Version.hpp"

namespace {

int failures = 0;

void expectOrder(std::string_view left, std::string_view right, int expected,
                 const char *what) {
  const int actual = launcher::util::compareVersions(left, right);
  const int normalized = actual < 0 ? -1 : actual > 0 ? 1 : 0;
  if (normalized == expected)
    return;
  std::fprintf(stderr, "FAIL: %s\n  left %.*s right %.*s actual %d\n", what,
               static_cast<int>(left.size()), left.data(),
               static_cast<int>(right.size()), right.data(), actual);
  ++failures;
}

void expectText(std::string_view actual, std::string_view expected,
                const char *what) {
  if (actual == expected)
    return;
  std::fprintf(stderr, "FAIL: %s\n  expected %.*s actual %.*s\n", what,
               static_cast<int>(expected.size()), expected.data(),
               static_cast<int>(actual.size()), actual.data());
  ++failures;
}

} // namespace

int main() {
  using launcher::util::compareVersions;
  using launcher::util::displayVersion;

  expectOrder("v1.0.0", "1.0.0", 0, "the v prefix is not part of the number");
  expectOrder("1.0", "1.0.0", 0, "missing parts count as zero");
  expectOrder("1.0.0", "1.0.1", -1, "a patch bump is newer");
  expectOrder("0.1.0", "1.0", -1, "the release tag is below the project build");
  expectOrder("10.0", "9.9.9", 1, "numbers compare as numbers, not text");
  expectOrder("1.0.0-rc1", "1.0.0", -1, "a pre-release is below its release");
  expectOrder("1.0.0", "1.0.0-rc2", 1, "the release is above its pre-release");
  expectOrder("1.2.3+8", "1.2.3", 0, "build metadata is not part of the order");
  expectOrder("v0.2.0", "v0.1.0", 1, "two release tags order normally");

  expectText(displayVersion("v1.2.3"), "1.2.3", "display drops the v");
  expectText(displayVersion("1.2.3+8"), "1.2.3", "display drops build metadata");
  expectText(displayVersion("0.1.0"), "0.1.0", "plain version is unchanged");

  if (failures != 0) {
    std::fprintf(stderr, "%d version checks failed\n", failures);
    return 1;
  }
  std::printf("version checks passed\n");
  return 0;
}
