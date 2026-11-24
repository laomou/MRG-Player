#include "util/Utf8Path.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace launcher::paths {
namespace {

#if defined(_WIN32)
std::wstring widen(std::string_view text) {
  if (text.empty())
    return {};
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0)
    return {};
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                      wide.data(), length);
  return wide;
}

std::string narrow(const std::wstring &text) {
  if (text.empty())
    return {};
  const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (length <= 0)
    return {};
  std::string utf8(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(),
                      length, nullptr, nullptr);
  return utf8;
}
#endif

} // namespace

std::filesystem::path fromUtf8(std::string_view text) {
#if defined(_WIN32)
  return std::filesystem::path(widen(text));
#else
  return std::filesystem::path(std::string(text));
#endif
}

std::string toUtf8(const std::filesystem::path &path) {
#if defined(_WIN32)
  return narrow(path.wstring());
#else
  return path.string();
#endif
}

} // namespace launcher::paths
