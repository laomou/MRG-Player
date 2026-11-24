#pragma once

#include <string>
#include <string_view>

namespace launcher::config {

[[nodiscard]] std::string dataRoot();
[[nodiscard]] std::string legacyDataRoot();
[[nodiscard]] bool migrateLegacyData(std::string &error);
// Directory for one class of runtime data, for example "saves" and "nes". The
// legacy launcher directory is migrated on first use and the directory is
// created if missing.
[[nodiscard]] std::string dataSubdirectory(std::string_view first, std::string_view second);

} // namespace launcher::config
