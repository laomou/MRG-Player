#pragma once

#include <optional>
#include <string>

namespace launcher::platform {

// Native "open file" dialog filtered to the formats the importer accepts.
std::optional<std::string> pickGameFile(std::string &error);

} // namespace launcher::platform
