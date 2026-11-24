#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace launcher::paths {

// Windows keeps paths as UTF-16 while the narrow std::filesystem and std::fstream
// overloads interpret char strings as the active code page, so a UTF-8 name from
// SDL, the file dialog, or metadata.tsv has to be converted explicitly. On other
// platforms the bytes are the path already.
[[nodiscard]] std::filesystem::path fromUtf8(std::string_view text);

// The inverse conversion: a path as UTF-8, whichever form the platform stores.
[[nodiscard]] std::string toUtf8(const std::filesystem::path &path);

} // namespace launcher::paths
