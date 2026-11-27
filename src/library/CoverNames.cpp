#include "library/CoverNames.hpp"

#include <cstdint>
#include <string_view>

namespace launcher::library {
namespace {

constexpr std::string_view kThumbnailBase = "https://raw.githubusercontent.com/libretro-thumbnails/";
constexpr std::string_view kBoxartPath = "/master/Named_Boxarts/";
constexpr std::string_view kSuffixes[] = {" (World)", " (USA)", " (Europe)", " (Japan)"};

std::string trim(const std::string &value) {
  const std::size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return {};
  const std::size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

bool equalsIgnoreCase(std::string_view left, std::string_view right) {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    char a = left[index];
    char b = right[index];
    if (a >= 'A' && a <= 'Z')
      a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z')
      b = static_cast<char>(b - 'A' + 'a');
    if (a != b)
      return false;
  }
  return true;
}

bool startsWith(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool appendUtf8(std::string &out, std::uint32_t code) {
  if (code <= 0x7FU) {
    out.push_back(static_cast<char>(code));
  } else if (code <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | (code >> 6)));
    out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
  } else if (code <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | (code >> 12)));
    out.push_back(static_cast<char>(0x80U | ((code >> 6) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (code >> 18)));
    out.push_back(static_cast<char>(0x80U | ((code >> 12) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((code >> 6) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
  }
  return true;
}

// A deliberately small JSON reader: cover-names.json is a flat object of
// strings, and anything else is reported instead of being guessed at.
class JsonReader {
 public:
  explicit JsonReader(const std::string &text) : text_(text) {}

  bool readObject(CoverNames &names, std::string &error) {
    skipSpace();
    if (!take('{')) {
      error = "The cover name file must start with a JSON object";
      return false;
    }
    skipSpace();
    if (take('}'))
      return true;
    while (true) {
      std::string key;
      std::string value;
      if (!readString(key) || !readSeparator(':') || !readString(value)) {
        error = "The cover name file has a malformed entry";
        return false;
      }
      names[trim(key)] = trim(value);
      skipSpace();
      if (take('}'))
        return true;
      if (!take(',')) {
        error = "The cover name file is missing a comma between entries";
        return false;
      }
    }
  }

 private:
  bool readString(std::string &out) {
    skipSpace();
    if (!take('"'))
      return false;
    out.clear();
    while (position_ < text_.size()) {
      const char ch = text_[position_++];
      if (ch == '"')
        return true;
      if (ch != '\\') {
        out.push_back(ch);
        continue;
      }
      if (position_ >= text_.size())
        return false;
      const char escape = text_[position_++];
      switch (escape) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          std::uint32_t code = 0;
          if (!readHex4(code))
            return false;
          if (code >= 0xD800U && code <= 0xDBFFU && position_ + 1 < text_.size() && text_[position_] == '\\' &&
              text_[position_ + 1] == 'u') {
            position_ += 2;
            std::uint32_t low = 0;
            if (!readHex4(low))
              return false;
            code = 0x10000U + ((code - 0xD800U) << 10) + (low - 0xDC00U);
          }
          appendUtf8(out, code);
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  bool readHex4(std::uint32_t &value) {
    if (position_ + 4 > text_.size())
      return false;
    value = 0;
    for (int digit = 0; digit < 4; ++digit) {
      const char ch = text_[position_++];
      value <<= 4;
      if (ch >= '0' && ch <= '9')
        value |= static_cast<std::uint32_t>(ch - '0');
      else if (ch >= 'a' && ch <= 'f')
        value |= static_cast<std::uint32_t>(ch - 'a' + 10);
      else if (ch >= 'A' && ch <= 'F')
        value |= static_cast<std::uint32_t>(ch - 'A' + 10);
      else
        return false;
    }
    return true;
  }

  bool take(char expected) {
    skipSpace();
    if (position_ >= text_.size() || text_[position_] != expected)
      return false;
    ++position_;
    return true;
  }

  bool readSeparator(char expected) {
    skipSpace();
    if (position_ >= text_.size() || text_[position_] != expected)
      return false;
    ++position_;
    return true;
  }

  void skipSpace() {
    while (position_ < text_.size()) {
      const char ch = text_[position_];
      if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
        break;
      ++position_;
    }
  }

  const std::string &text_;
  std::size_t position_ = 0;
};

const std::string *findMappedName(const CoverNames &names, const std::string &title) {
  const std::string wanted = trim(title);
  for (const auto &entry : names) {
    if (equalsIgnoreCase(entry.first, wanted) && !entry.second.empty())
      return &entry.second;
  }
  return nullptr;
}

}  // namespace

bool parseCoverNames(const std::string &json, CoverNames &names, std::string &error) {
  names.clear();
  error.clear();
  JsonReader reader(json);
  CoverNames parsed;
  if (!reader.readObject(parsed, error))
    return false;
  names = std::move(parsed);
  return true;
}

const char *coverNamesTemplate() noexcept {
  return "{\n"
         "  \"F1赛车\": \"F-1 Race (Japan)\",\n"
         "  \"超级马里奥\": \"Super Mario Bros. (World)\",\n"
         "  \"俄罗斯方块\": \"Tetris (USA)\"\n"
         "}\n";
}

std::string coverRepository(game::GamePlatform platform) {
  switch (platform) {
    case game::GamePlatform::Nes:
      return "Nintendo_-_Nintendo_Entertainment_System";
    case game::GamePlatform::GameBoy:
      return "Nintendo_-_Game_Boy";
    case game::GamePlatform::Gba:
      return "Nintendo_-_Game_Boy_Advance";
    case game::GamePlatform::Snes:
      return "Nintendo_-_Super_Nintendo_Entertainment_System";
    case game::GamePlatform::Genesis:
      return "Sega_-_Mega_Drive_-_Genesis";
    default:
      return {};
  }
}

std::vector<std::string> coverNameCandidates(const std::string &title, const CoverNames &names) {
  std::vector<std::string> candidates;
  const std::string base = trim(title);
  if (const std::string *mapped = findMappedName(names, base)) {
    candidates.push_back(*mapped);
  }
  if (!base.empty()) {
    candidates.push_back(base);
    for (std::string_view suffix : kSuffixes)
      candidates.push_back(base + std::string(suffix));
  }
  std::vector<std::string> unique;
  for (const std::string &candidate : candidates) {
    bool seen = false;
    for (const std::string &existing : unique)
      seen = seen || equalsIgnoreCase(existing, candidate);
    if (!seen)
      unique.push_back(candidate);
  }
  return unique;
}

std::string urlEncode(const std::string &value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(value.size());
  for (const unsigned char ch : value) {
    const bool unreserved = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                            ch == '-' || ch == '_' || ch == '.' || ch == '~';
    if (unreserved) {
      encoded.push_back(static_cast<char>(ch));
    } else {
      encoded.push_back('%');
      encoded.push_back(kHex[ch >> 4]);
      encoded.push_back(kHex[ch & 0x0FU]);
    }
  }
  return encoded;
}

std::vector<std::string> coverUrls(game::GamePlatform platform, const std::string &title, const CoverNames &names) {
  std::vector<std::string> urls;
  const std::string repository = coverRepository(platform);
  for (const std::string &candidate : coverNameCandidates(title, names)) {
    if (startsWith(candidate, "http://") || startsWith(candidate, "https://")) {
      urls.push_back(candidate);
      continue;
    }
    if (repository.empty())
      continue;
    urls.push_back(std::string(kThumbnailBase) + repository + std::string(kBoxartPath) + urlEncode(candidate) +
                    ".png");
  }
  return urls;
}

std::string coverFileName(const std::string &title) {
  constexpr std::string_view kForbidden = "/\\:*?\"<>|";
  std::string name;
  name.reserve(title.size());
  for (char value : title) {
    const bool forbidden = kForbidden.find(value) != std::string_view::npos;
    const bool control = static_cast<unsigned char>(value) < 0x20;
    name.push_back(forbidden || control ? '_' : value);
  }
  // Windows drops trailing dots and spaces, and the stock launcher would end up
  // looking for a name that cannot exist.
  while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
    name.pop_back();
  if (name.empty())
    name = "cover";
  return name;
}

std::string coverTargetPath(const std::string &root, game::GamePlatform platform, const std::string &title) {
  const std::string system(game::platformDescriptor(platform).cardDirectory);
  if (root.empty() || system.empty())
    return {};
  std::string base = root;
  while (!base.empty() && (base.back() == '/' || base.back() == '\\'))
    base.pop_back();
  if (base.empty())
    return {};
  return base + "/" + system + "/" + coverFileName(title) + ".png";
}

}  // namespace launcher::library
