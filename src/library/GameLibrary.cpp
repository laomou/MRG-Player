#include "library/GameLibrary.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include "config/AppPaths.hpp"
#include "util/Utf8Path.hpp"

namespace launcher::library {
namespace {
namespace fs = std::filesystem;
// Ten card positions on the shelf, always: the eleventh slot is the "more"
// tile, so an empty library still shows a full row instead of a short one.
constexpr std::size_t kPlaceholderCount = 10;
constexpr std::uintmax_t kMaximumGameSize = 64U * 1024U * 1024U;
// V3 replaced the managed copies with the path of the ROM on the card or in the
// ROM folder; older files still load and are rewritten as V3 on the next save.
constexpr const char *kMetadataHeader = "MRG_LIBRARY_METADATA_V3";
constexpr const char *kMetadataHeaderV2 = "MRG_LIBRARY_METADATA_V2";
constexpr const char *kMetadataHeaderV1 = "MRG_LIBRARY_METADATA_V1";
constexpr std::size_t kMetadataFieldCountV1 = 6;
constexpr std::size_t kMetadataFieldCountV2 = 10;
constexpr std::size_t kMetadataFieldCountV3 = 11;

std::string lowerAscii(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string hashSuffix(std::uint64_t hash) {
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

bool isHexSuffix(std::string_view value) {
  return value.size() == 16 &&
         std::all_of(value.begin(), value.end(),
                     [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string displayTitle(const fs::path &path) {
  std::string title = paths::toUtf8(path.stem());
  const std::size_t separator = title.rfind("__");
  if (separator != std::string::npos &&
      isHexSuffix(std::string_view(title).substr(separator + 2)))
    title.resize(separator);
  return title.empty() ? std::string("UNTITLED") : title;
}

// The library indexes the file instead of copying it, so the only thing an
// import does with the bytes is fold them into the content id.
bool hashGameFile(const fs::path &path, game::GamePlatform platform,
                  std::string &id, std::string &error) {
  if (!game::validateGameFile(platform, paths::toUtf8(path), error))
    return false;
  std::error_code ec;
  const std::uintmax_t size = fs::file_size(path, ec);
  if (ec || size > kMaximumGameSize) {
    error = ec ? "Cannot read game file size: " + ec.message()
               : "Game exceeds the 64 MiB import limit";
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Cannot read game file";
    return false;
  }
  std::uint64_t hash = 14695981039346656037ULL;
  std::array<char, 64U * 1024U> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize got = input.gcount();
    for (std::streamsize index = 0; index < got; ++index) {
      hash ^= static_cast<unsigned char>(
          buffer[static_cast<std::size_t>(index)]);
      hash *= 1099511628211ULL;
    }
  }
  if (!input.eof()) {
    error = "Cannot read game file";
    return false;
  }
  id = hashSuffix(hash);
  return true;
}

std::string absolutePath(const fs::path &path) {
  std::error_code ec;
  const fs::path absolute = fs::absolute(path, ec);
  return paths::toUtf8(ec ? path : absolute);
}

// True when path sits inside directory, resolving the two the same way so a
// card that folds case or a symlinked data root still compares equal.
bool insideDirectory(const fs::path &path, const fs::path &directory) {
  if (path.empty() || directory.empty())
    return false;
  std::error_code ec;
  fs::path child = fs::weakly_canonical(path.parent_path(), ec);
  if (ec) {
    ec.clear();
    child = path.parent_path();
  }
  fs::path base = fs::weakly_canonical(directory, ec);
  if (ec)
    base = directory;
  auto basePart = base.begin();
  for (auto childPart = child.begin(); basePart != base.end();
       ++basePart, ++childPart) {
    if (childPart == child.end() || *childPart != *basePart)
      return false;
  }
  return true;
}

std::string encodeField(std::string_view value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string encoded;
  for (const unsigned char byte : value) {
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') || byte == ' ' || byte == '-' ||
        byte == '_' || byte >= 0x80) {
      encoded.push_back(static_cast<char>(byte));
    } else {
      encoded.push_back('%');
      encoded.push_back(kHex[byte >> 4U]);
      encoded.push_back(kHex[byte & 0x0fU]);
    }
  }
  return encoded;
}

int hexValue(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}

bool decodeField(std::string_view encoded, std::string &value) {
  value.clear();
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    if (encoded[index] != '%') {
      value.push_back(encoded[index]);
      continue;
    }
    if (index + 2 >= encoded.size())
      return false;
    const int high = hexValue(encoded[index + 1]);
    const int low = hexValue(encoded[index + 2]);
    if (high < 0 || low < 0)
      return false;
    value.push_back(static_cast<char>((high << 4) | low));
    index += 2;
  }
  return true;
}

std::vector<std::string> splitMetadataLine(const std::string &line) {
  std::vector<std::string> fields;
  std::size_t begin = 0;
  while (true) {
    const std::size_t end = line.find('	', begin);
    if (end == std::string::npos) {
      fields.push_back(line.substr(begin));
      return fields;
    }
    fields.push_back(line.substr(begin, end - begin));
    begin = end + 1;
  }
}

// The copy an earlier build made of this game, if it is still on the disk. The
// name ended in "__<id>", which is enough to recognise it by content alone.
fs::path legacyManagedFile(const fs::path &directory,
                           game::GamePlatform platform,
                           const std::string &id) {
  if (directory.empty() || id.empty())
    return {};
  const game::PlatformDescriptor &descriptor =
      game::platformDescriptor(platform);
  const std::string suffix = "__" + id;
  const std::string_view folders[] = {descriptor.managedDirectory,
                                      descriptor.legacyManagedDirectory};
  for (const std::string_view folder : folders) {
    if (folder.empty())
      continue;
    const fs::path platformDirectory = directory / paths::fromUtf8(std::string(folder));
    std::error_code ec;
    if (!fs::is_directory(platformDirectory, ec))
      continue;
    for (const fs::directory_entry &item :
         fs::directory_iterator(platformDirectory, ec)) {
      if (!item.is_regular_file(ec)) {
        ec.clear();
        continue;
      }
      const std::string stem = paths::toUtf8(item.path().stem());
      if (stem.size() <= suffix.size())
        continue;
      if (stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) ==
          0)
        return item.path();
    }
  }
  return {};
}

} // namespace

std::string contentIdForPath(const std::string &path,
                             game::GamePlatform platform) {
  if (platform == game::GamePlatform::Unknown)
    return {};
  std::string id;
  std::string ignored;
  if (!hashGameFile(paths::fromUtf8(path), platform, id, ignored))
    return {};
  return id;
}

std::string titleForPath(const std::string &path) {
  return displayTitle(paths::fromUtf8(path));
}

GameLibrary::GameLibrary(std::string directoryOverride)
    : directoryOverride_(std::move(directoryOverride)) {}

bool GameLibrary::initialize() {
  error_.clear();
  if (!directoryOverride_.empty()) {
    directory_ = directoryOverride_;
  } else {
    const std::string root = config::dataRoot();
    if (root.empty()) {
      error_ = "Cannot locate MRG Player data directory";
      return false;
    }
    directory_ = paths::toUtf8(paths::fromUtf8(root) / "library");
  }
  std::error_code ec;
  fs::create_directories(paths::fromUtf8(directory_), ec);
  if (ec) {
    error_ = "Cannot create game library directory: " + ec.message();
    return false;
  }
  if (!loadMetadata())
    return false;
  return refresh();
}

ImportStatus GameLibrary::importGame(const std::string &sourcePath) {
  error_.clear();
  lastImportedIndex_ = -1;
  if (directory_.empty() && !initialize())
    return ImportStatus::Failed;

  const fs::path source = paths::fromUtf8(sourcePath);
  const game::GamePlatform platform = game::detectPlatform(sourcePath);
  if (platform == game::GamePlatform::Unknown) {
    error_ = "No installed runtime supports this game format";
    return ImportStatus::Failed;
  }

  std::string id;
  if (!hashGameFile(source, platform, id, error_))
    return ImportStatus::Failed;

  // The library keeps the file where the player put it and remembers the path;
  // nothing is copied, so re-importing the same bytes only has to re-point an
  // entry that moved or drop a copy an older build left behind.
  const std::string path = absolutePath(source);
  Metadata &metadata = metadata_[metadataKey(id, platform)];
  const bool known = !metadata.path.empty();
  if (known && metadata.path == path) {
    lastImportedIndex_ = indexOfPath(path);
    return ImportStatus::AlreadyPresent;
  }
  if (known) {
    std::error_code ec;
    const bool present =
        fs::is_regular_file(paths::fromUtf8(metadata.path), ec);
    const bool copyInLibrary =
        insideDirectory(paths::fromUtf8(metadata.path),
                        paths::fromUtf8(directory_));
    if (present && !copyInLibrary) {
      // The same bytes already live somewhere else; the first path wins.
      lastImportedIndex_ = indexOfPath(metadata.path);
      return ImportStatus::AlreadyPresent;
    }
    const std::string previous = metadata.path;
    metadata.path = path;
    if (copyInLibrary && previous != path) {
      std::error_code ignored;
      fs::remove(paths::fromUtf8(previous), ignored);
    }
  }
  if (!known)
    metadata.path = path;
  if (!saveMetadata() || !refresh())
    return ImportStatus::Failed;
  lastImportedIndex_ = indexOfPath(path);
  return known ? ImportStatus::AlreadyPresent : ImportStatus::Imported;
}

bool GameLibrary::removeGame(std::size_t index) {
  error_.clear();
  lastImportedIndex_ = -1;
  if (index >= entries_.size()) {
    error_ = "Selected game is not in the managed library";
    return false;
  }
  const GameEntry entry = entries_[index];
  const auto found = metadata_.find(metadataKey(entry.id, entry.platform));
  if (found == metadata_.end()) {
    error_ = "Selected game is not in the managed library";
    return false;
  }
  // Forgetting a game must never delete a file the player put on the card. The
  // one exception is a copy an older build made: that file is ours, not theirs,
  // and leaving it behind would let the next scan adopt it right back.
  const std::string forgotten = found->second.path;
  found->second.path.clear();
  if (!forgotten.empty() &&
      insideDirectory(paths::fromUtf8(forgotten), paths::fromUtf8(directory_))) {
    std::error_code ignored;
    fs::remove(paths::fromUtf8(forgotten), ignored);
  }
  return saveMetadata() && refresh();
}

bool GameLibrary::setFavorite(std::size_t index, bool favorite) {
  error_.clear();
  if (index >= entries_.size()) {
    error_ = "Selected game is not in the managed library";
    return false;
  }
  const GameEntry entry = entries_[index];
  metadata_[metadataKey(entry.id, entry.platform)].favorite = favorite;
  return saveMetadata() && refresh();
}

bool GameLibrary::setCustomTitle(std::size_t index, std::string title) {
  error_.clear();
  if (index >= entries_.size()) {
    error_ = "Selected game is not in the managed library";
    return false;
  }
  if (title.size() > 240) {
    error_ = "Custom title is too long";
    return false;
  }
  const GameEntry entry = entries_[index];
  metadata_[metadataKey(entry.id, entry.platform)].customTitle =
      std::move(title);
  return saveMetadata() && refresh();
}

bool GameLibrary::recordPlaySession(const std::string &id,
                                    game::GamePlatform platform,
                                    std::uint64_t seconds,
                                    std::int64_t lastPlayed) {
  error_.clear();
  Metadata &metadata = metadata_[metadataKey(id, platform)];
  metadata.playSeconds =
      seconds > std::numeric_limits<std::uint64_t>::max() - metadata.playSeconds
          ? std::numeric_limits<std::uint64_t>::max()
          : metadata.playSeconds + seconds;
  metadata.lastPlayed = std::max(metadata.lastPlayed, lastPlayed);
  return saveMetadata() && refresh();
}

bool GameLibrary::videoOverride(const std::string &id,
                                game::GamePlatform platform,
                                runtime::VideoOptions &options) const noexcept {
  const auto found = metadata_.find(metadataKey(id, platform));
  if (found == metadata_.end() || !found->second.hasVideoOverride)
    return false;
  options = found->second.video;
  return true;
}

bool GameLibrary::setVideoOverride(const std::string &id,
                                   game::GamePlatform platform,
                                   const runtime::VideoOptions &options) {
  error_.clear();
  Metadata &metadata = metadata_[metadataKey(id, platform)];
  metadata.hasVideoOverride = true;
  metadata.video = options;
  return saveMetadata();
}

bool GameLibrary::clearVideoOverride(const std::string &id,
                                     game::GamePlatform platform) {
  error_.clear();
  const auto found = metadata_.find(metadataKey(id, platform));
  if (found == metadata_.end() || !found->second.hasVideoOverride)
    return true;
  found->second.hasVideoOverride = false;
  return saveMetadata();
}

const std::vector<GameEntry> &GameLibrary::entries() const noexcept {
  return entries_;
}
std::size_t GameLibrary::slotCount() const noexcept {
  return std::max(kPlaceholderCount, entries_.size());
}
const std::string &GameLibrary::directory() const noexcept {
  return directory_;
}
std::string_view GameLibrary::error() const noexcept { return error_; }
int GameLibrary::lastImportedIndex() const noexcept {
  return lastImportedIndex_;
}

int GameLibrary::indexOfPath(const std::string &path) const noexcept {
  const auto found = std::find_if(
      entries_.begin(), entries_.end(),
      [&](const GameEntry &entry) { return entry.filePath == path; });
  return found == entries_.end()
             ? -1
             : static_cast<int>(std::distance(entries_.begin(), found));
}

int GameLibrary::indexOfId(const std::string &id,
                           game::GamePlatform platform) const noexcept {
  const auto found = std::find_if(
      entries_.begin(), entries_.end(), [&](const GameEntry &entry) {
        return entry.id == id && entry.platform == platform;
      });
  return found == entries_.end()
             ? -1
             : static_cast<int>(std::distance(entries_.begin(), found));
}

std::string GameLibrary::metadataKey(const std::string &id,
                                     game::GamePlatform platform) {
  return std::to_string(static_cast<int>(platform)) + ":" + id;
}

bool GameLibrary::loadMetadata() {
  metadata_.clear();
  std::ifstream input(paths::fromUtf8(directory_) / "metadata.tsv");
  if (!input)
    return true;
  std::string line;
  if (!std::getline(input, line)) {
    error_ = "Game library metadata has an unsupported format";
    return false;
  }
  const bool version3 = line == kMetadataHeader;
  const bool version2 = line == kMetadataHeaderV2;
  if (!version3 && !version2 && line != kMetadataHeaderV1) {
    error_ = "Game library metadata has an unsupported format";
    return false;
  }
  const std::size_t expectedFields =
      version3 ? kMetadataFieldCountV3
               : (version2 ? kMetadataFieldCountV2 : kMetadataFieldCountV1);
  while (std::getline(input, line)) {
    if (line.empty())
      continue;
    const std::vector<std::string> fields = splitMetadataLine(line);
    if (fields.size() != expectedFields)
      continue;
    try {
      const int platformValue = std::stoi(fields[0]);
      const auto platform = static_cast<game::GamePlatform>(platformValue);
      Metadata metadata;
      metadata.favorite = fields[2] == "1";
      metadata.playSeconds = std::stoull(fields[3]);
      metadata.lastPlayed = std::stoll(fields[4]);
      const std::size_t titleField = expectedFields - 1;
      if (version3 || version2) {
        metadata.hasVideoOverride = fields[5] == "1";
        metadata.video.aspect = static_cast<runtime::AspectMode>(
            std::clamp(std::stoi(fields[6]), 0, 2));
        metadata.video.integerScale = fields[7] == "1";
        metadata.video.overscan =
            std::clamp(std::stoi(fields[8]), 0, runtime::kMaxOverscan);
      }
      if (version3 && !decodeField(fields[9], metadata.path))
        continue;
      if (!decodeField(fields[titleField], metadata.customTitle))
        continue;
      metadata_[metadataKey(fields[1], platform)] = std::move(metadata);
    } catch (...) {
      continue;
    }
  }
  return true;
}

bool GameLibrary::saveMetadata() {
  const fs::path destination = paths::fromUtf8(directory_) / "metadata.tsv";
  fs::path temporary = destination;
  temporary += fs::path(".tmp");
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
      error_ = "Cannot write game library metadata";
      return false;
    }
    output << kMetadataHeader << '\n';
    std::vector<std::string> keys;
    keys.reserve(metadata_.size());
    for (const auto &item : metadata_)
      keys.push_back(item.first);
    std::sort(keys.begin(), keys.end());
    for (const std::string &key : keys) {
      const std::size_t separator = key.find(':');
      if (separator == std::string::npos)
        continue;
      const Metadata &metadata = metadata_.at(key);
      output << key.substr(0, separator) << '	' << key.substr(separator + 1)
             << '	' << (metadata.favorite ? 1 : 0) << '	'
             << metadata.playSeconds << '	' << metadata.lastPlayed << '	'
             << (metadata.hasVideoOverride ? 1 : 0) << '	'
             << static_cast<int>(metadata.video.aspect) << '	'
             << (metadata.video.integerScale ? 1 : 0) << '	'
             << metadata.video.overscan << '	' << encodeField(metadata.path)
             << '	' << encodeField(metadata.customTitle) << '\n';
    }
    if (!output) {
      error_ = "Cannot finish game library metadata";
      return false;
    }
  }
  std::error_code ec;
  fs::rename(temporary, destination, ec);
  if (ec) {
    std::error_code ignored;
    fs::remove(destination, ignored);
    ec.clear();
    fs::rename(temporary, destination, ec);
  }
  if (ec) {
    error_ = "Cannot install game library metadata: " + ec.message();
    std::error_code ignored;
    fs::remove(temporary, ignored);
    return false;
  }
  return true;
}

bool GameLibrary::refresh() {
  entries_.clear();
  // The shelf is the metadata: an entry shows up when its file is still there,
  // and an entry whose file is gone keeps its save data and is left alone until
  // the player points the launcher at the game again.
  std::error_code ec;
  bool adopted = false;
  for (auto &item : metadata_) {
    const std::size_t separator = item.first.find(':');
    if (separator == std::string::npos)
      continue;
    game::GamePlatform platform = game::GamePlatform::Unknown;
    try {
      platform = static_cast<game::GamePlatform>(
          std::stoi(item.first.substr(0, separator)));
    } catch (...) {
      continue;
    }
    if (platform == game::GamePlatform::Unknown)
      continue;
    const std::string id = item.first.substr(separator + 1);
    Metadata &details = item.second;
    if (!details.path.empty() &&
        !fs::is_regular_file(paths::fromUtf8(details.path), ec))
      details.path.clear();
    ec.clear();
    if (details.path.empty()) {
      // An entry written before the library remembered paths: take over the
      // copy the old build made, once, and remember where it is from now on.
      const fs::path copy =
          legacyManagedFile(paths::fromUtf8(directory_), platform, id);
      if (copy.empty())
        continue;
      details.path = paths::toUtf8(copy);
      adopted = true;
    }
    std::string validationError;
    if (!game::validateGameFile(platform, details.path, validationError))
      continue;
    const std::string title = details.customTitle.empty()
                                  ? displayTitle(paths::fromUtf8(details.path))
                                  : details.customTitle;
    entries_.push_back({id, title, details.path, platform, details.favorite,
                        details.playSeconds, details.lastPlayed});
  }
  if (adopted)
    (void)saveMetadata();
  std::sort(entries_.begin(), entries_.end(),
            [](const GameEntry &left, const GameEntry &right) {
              if (left.favorite != right.favorite)
                return left.favorite > right.favorite;
              if (left.platform != right.platform)
                return static_cast<int>(left.platform) <
                       static_cast<int>(right.platform);
              return lowerAscii(left.title) < lowerAscii(right.title);
            });
  return true;
}

} // namespace launcher::library
