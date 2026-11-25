#pragma once

#include <cstdint>
#include <string>

namespace launcher::io {

// Downloads a URL into a file, replacing whatever was there. On Windows this
// goes through WinHTTP so a stock install does not need curl.exe; elsewhere it
// shells out to curl and falls back to wget. Bytes beyond maximumBytes abort
// the download, and written reports what actually landed on disk.
[[nodiscard]] bool downloadFile(const std::string &url, const std::string &target, std::uint64_t maximumBytes,
                                std::uint64_t &written, std::string &error);

}  // namespace launcher::io
