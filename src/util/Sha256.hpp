#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace launcher::util {

// Lower-case hex SHA-256 of a buffer, used to pin downloaded core archives to
// the exact bytes a release was tested against.
[[nodiscard]] std::string sha256Hex(const std::uint8_t *data, std::size_t size);
[[nodiscard]] std::string sha256File(const std::string &path);

} // namespace launcher::util
