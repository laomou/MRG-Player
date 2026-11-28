#include "util/Version.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace launcher::util {
namespace {

struct ParsedVersion {
  std::vector<unsigned long long> numbers;
  std::string preRelease;
};

ParsedVersion parse(std::string_view text) {
  ParsedVersion parsed;
  while (!text.empty() && (text.front() == ' ' || text.front() == 'v' || text.front() == 'V'))
    text.remove_prefix(1);
  const std::size_t suffix = text.find_first_of("-+");
  const std::string_view core = text.substr(0, suffix);
  if (suffix != std::string_view::npos && text[suffix] == '-')
    parsed.preRelease = std::string(text.substr(suffix + 1));

  unsigned long long value = 0;
  bool haveDigits = false;
  for (const char character : core) {
    if (std::isdigit(static_cast<unsigned char>(character))) {
      value = value * 10 + static_cast<unsigned long long>(character - '0');
      haveDigits = true;
    } else if (haveDigits) {
      parsed.numbers.push_back(value);
      value = 0;
      haveDigits = false;
    }
  }
  if (haveDigits)
    parsed.numbers.push_back(value);
  if (parsed.numbers.empty())
    parsed.numbers.push_back(0);
  return parsed;
}

} // namespace

int compareVersions(std::string_view left, std::string_view right) {
  const ParsedVersion a = parse(left);
  const ParsedVersion b = parse(right);
  const std::size_t count = std::max(a.numbers.size(), b.numbers.size());
  for (std::size_t index = 0; index < count; ++index) {
    const unsigned long long av = index < a.numbers.size() ? a.numbers[index] : 0;
    const unsigned long long bv = index < b.numbers.size() ? b.numbers[index] : 0;
    if (av != bv)
      return av < bv ? -1 : 1;
  }
  if (a.preRelease == b.preRelease)
    return 0;
  if (a.preRelease.empty())
    return 1;
  if (b.preRelease.empty())
    return -1;
  return a.preRelease < b.preRelease ? -1 : 1;
}

std::string displayVersion(std::string_view version) {
  while (!version.empty() && (version.front() == ' ' || version.front() == 'v' || version.front() == 'V'))
    version.remove_prefix(1);
  const std::size_t plus = version.find('+');
  if (plus != std::string_view::npos)
    version = version.substr(0, plus);
  return std::string(version);
}

} // namespace launcher::util
