#include "app/RenderSurface.hpp"

#include <cctype>
#include <cstdlib>

namespace launcher {
namespace {

constexpr int kMaxDimension = 8192;

std::string_view trim(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.remove_prefix(1);
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.remove_suffix(1);
  return value;
}

bool parseNumber(std::string_view text, int &number) {
  if (text.empty())
    return false;
  int parsed = 0;
  for (const char character : text) {
    if (std::isdigit(static_cast<unsigned char>(character)) == 0)
      return false;
    if (parsed > (kMaxDimension - (character - '0')) / 10)
      return false;
    parsed = parsed * 10 + (character - '0');
  }
  if (parsed <= 0 || parsed > kMaxDimension)
    return false;
  number = parsed;
  return true;
}

} // namespace

OutputSize parseOutputSizeOverride(std::string_view value) {
  const std::string_view trimmed = trim(value);
  const std::size_t separator = trimmed.find_first_of("xX*");
  if (separator == std::string_view::npos)
    return {};
  OutputSize size;
  if (!parseNumber(trim(trimmed.substr(0, separator)), size.width) ||
      !parseNumber(trim(trimmed.substr(separator + 1)), size.height))
    return {};
  return size;
}

OutputSize resolveOutputSize(int reportedWidth, int reportedHeight,
                             int windowWidth, int windowHeight,
                             const char *overrideValue) {
  if (overrideValue != nullptr && overrideValue[0] != '\0') {
    const OutputSize override = parseOutputSizeOverride(overrideValue);
    if (override.valid())
      return override;
  }
  if (reportedWidth > 0 && reportedHeight > 0)
    return {reportedWidth, reportedHeight};
  if (windowWidth > 0 && windowHeight > 0)
    return {windowWidth, windowHeight};
  return {};
}

} // namespace launcher
