#pragma once

#include <cstdint>
#include <string>

namespace launcher::io {

// What happened to a download. Callers need the difference: a missing picture
// is normal and should move on to the next candidate, while an unreachable
// host means every later request will fail the same way.
enum class DownloadStatus {
  Ok,
  NotFound,  // The server answered, but not with the file.
  Network,   // Name resolution, connection or timeout: the host is unreachable.
  Failed,    // The file could not be written or was rejected.
};

// Downloads a URL into a file, replacing whatever was there. On Windows this
// goes through WinHTTP so a stock install does not need curl.exe; elsewhere it
// shells out to curl and falls back to wget. Bytes beyond maximumBytes abort
// the download, and written reports what actually landed on disk. The fetcher
// never writes to the launcher's terminal: its errors come back as a status
// and a message.
[[nodiscard]] DownloadStatus downloadFile(const std::string &url, const std::string &target,
                                          std::uint64_t maximumBytes, std::uint64_t &written,
                                          std::string &error,
                                          const std::string &caBundle = {});

}  // namespace launcher::io
