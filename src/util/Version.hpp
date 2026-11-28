#pragma once

#include <string>
#include <string_view>

namespace launcher::util {

// Orders two release strings the way a person reads them: leading "v" is
// ignored, each dot-separated number is compared as a number, missing parts
// count as zero, and a pre-release ("1.0.0-rc1") sorts below the release it
// leads to. Non-numeric trailing text is ignored rather than treated as a
// failure, because tags in the wild are not always tidy.
[[nodiscard]] int compareVersions(std::string_view left, std::string_view right);

// The part that should be shown to a player: no leading "v", no build metadata.
[[nodiscard]] std::string displayVersion(std::string_view version);

} // namespace launcher::util
