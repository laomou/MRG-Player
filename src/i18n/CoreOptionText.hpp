#pragma once

#include <string>
#include <string_view>

#include "i18n/Localization.hpp"

namespace launcher::i18n {

// Core options are declared by the core itself, in English. These helpers
// answer with a Chinese label when we know the option, and fall back to the
// text the core supplied otherwise. Lookups never change the values we hand
// back to the core, so only the panel is affected.
[[nodiscard]] std::string coreOptionLabel(std::string_view key, std::string_view fallback,
                                          Language language);
[[nodiscard]] std::string coreOptionValue(std::string_view value, std::string_view fallback,
                                          Language language);

}  // namespace launcher::i18n
