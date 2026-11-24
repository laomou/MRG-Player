#include "runtime/libretro/CoreArchive.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(MRG_HAVE_ZLIB)
#include <zlib.h>
#endif

#include "util/Utf8Path.hpp"

namespace launcher::runtime::libretro {
namespace {

namespace fs = std::filesystem;

#if defined(MRG_HAVE_ZLIB)

// A core is a handful of megabytes, so reading the whole archive keeps the
// parser small; the caps below only stop a hostile file from exhausting memory.
constexpr std::uint64_t kMaximumArchiveBytes = 128U * 1024U * 1024U;
constexpr std::uint64_t kMaximumCoreBytes = 256U * 1024U * 1024U;
constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054b50U;
constexpr std::uint32_t kCentralDirectorySignature = 0x02014b50U;
constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50U;
constexpr std::uint16_t kFlagEncrypted = 0x0001U;
constexpr std::uint32_t kZip64Marker = 0xFFFFFFFFU;
constexpr std::size_t kEndOfCentralDirectoryBytes = 22;

std::uint16_t read16(const std::uint8_t *data) {
  return static_cast<std::uint16_t>(data[0] | (static_cast<std::uint16_t>(data[1]) << 8));
}

std::uint32_t read32(const std::uint8_t *data) {
  return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

bool inRange(std::size_t offset, std::size_t length, std::size_t size) {
  return offset <= size && length <= size - offset;
}

bool readFile(const std::string &path, std::vector<std::uint8_t> &bytes, std::string &error) {
  std::error_code code;
  const fs::path filePath = paths::fromUtf8(path);
  const std::uint64_t size = fs::file_size(filePath, code);
  if (code || size == 0) {
    error = "cannot read " + path;
    return false;
  }
  if (size > kMaximumArchiveBytes) {
    error = "the archive is larger than 128 MiB";
    return false;
  }
  std::ifstream input(filePath, std::ios::binary);
  if (!input) {
    error = "cannot open " + path;
    return false;
  }
  bytes.resize(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    error = "cannot read " + path;
    return false;
  }
  return true;
}

std::string baseName(const std::string &entry) {
  const std::size_t separator = entry.find_last_of("/\\");
  return separator == std::string::npos ? entry : entry.substr(separator + 1);
}

// The end record sits at the tail, but a trailing comment may push it back up to
// 64 KiB, so scan backwards from the end.
bool findEndOfCentralDirectory(const std::vector<std::uint8_t> &bytes, std::size_t &offset) {
  if (bytes.size() < kEndOfCentralDirectoryBytes)
    return false;
  const std::size_t last = bytes.size() - kEndOfCentralDirectoryBytes;
  const std::size_t lowest = last > 0xFFFFU ? last - 0xFFFFU : 0;
  for (std::size_t position = last + 1; position-- > lowest;) {
    if (read32(bytes.data() + position) == kEndOfCentralDirectorySignature) {
      offset = position;
      return true;
    }
  }
  return false;
}

struct Entry {
  std::uint16_t flags = 0;
  std::uint16_t method = 0;
  std::uint32_t crc = 0;
  std::uint32_t compressedSize = 0;
  std::uint32_t uncompressedSize = 0;
  std::uint32_t localOffset = 0;
};

bool findEntry(const std::vector<std::uint8_t> &bytes, std::size_t directoryOffset, std::uint16_t count,
               const std::string &expectedFileName, Entry &entry, std::string &error) {
  std::size_t position = directoryOffset;
  for (std::uint16_t index = 0; index < count; ++index) {
    if (!inRange(position, 46, bytes.size())) {
      error = "the central directory is truncated";
      return false;
    }
    const std::uint8_t *header = bytes.data() + position;
    if (read32(header) != kCentralDirectorySignature) {
      error = "the central directory is corrupt";
      return false;
    }
    const std::uint16_t nameLength = read16(header + 28);
    const std::uint16_t extraLength = read16(header + 30);
    const std::uint16_t commentLength = read16(header + 32);
    if (!inRange(position + 46, nameLength, bytes.size())) {
      error = "an entry name runs past the archive";
      return false;
    }
    const std::string name(reinterpret_cast<const char *>(header + 46), nameLength);
    if (baseName(name) == expectedFileName) {
      entry.flags = read16(header + 8);
      entry.method = read16(header + 10);
      entry.crc = read32(header + 16);
      entry.compressedSize = read32(header + 20);
      entry.uncompressedSize = read32(header + 24);
      entry.localOffset = read32(header + 42);
      return true;
    }
    position += 46 + nameLength + extraLength + commentLength;
  }
  error = "the archive does not contain " + expectedFileName;
  return false;
}

bool inflateRaw(const std::uint8_t *source, std::size_t sourceSize, std::size_t expectedSize,
                std::vector<std::uint8_t> &output) {
  output.assign(expectedSize, 0);
  z_stream stream{};
  if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
    return false;
  stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(source));
  stream.avail_in = static_cast<uInt>(sourceSize);
  stream.next_out = reinterpret_cast<Bytef *>(output.data());
  stream.avail_out = static_cast<uInt>(output.size());
  const int status = inflate(&stream, Z_FINISH);
  const bool complete = status == Z_STREAM_END && stream.total_out == expectedSize;
  inflateEnd(&stream);
  return complete;
}

bool extract(const std::vector<std::uint8_t> &bytes, const Entry &entry, std::vector<std::uint8_t> &content,
             std::string &error) {
  if ((entry.flags & kFlagEncrypted) != 0) {
    error = "the archive entry is encrypted";
    return false;
  }
  if (entry.compressedSize == kZip64Marker || entry.uncompressedSize == kZip64Marker ||
      entry.localOffset == kZip64Marker) {
    error = "zip64 archives are not supported";
    return false;
  }
  if (entry.uncompressedSize == 0 || entry.uncompressedSize > kMaximumCoreBytes) {
    error = "the archive entry has an unreasonable size";
    return false;
  }
  if (!inRange(entry.localOffset, 30, bytes.size())) {
    error = "the local header runs past the archive";
    return false;
  }
  const std::uint8_t *header = bytes.data() + entry.localOffset;
  if (read32(header) != kLocalHeaderSignature) {
    error = "the local header is corrupt";
    return false;
  }
  const std::size_t dataOffset = entry.localOffset + 30 + read16(header + 26) + read16(header + 28);
  if (!inRange(dataOffset, entry.compressedSize, bytes.size())) {
    error = "the compressed data runs past the archive";
    return false;
  }
  const std::uint8_t *source = bytes.data() + dataOffset;
  if (entry.method == 0) {
    if (entry.compressedSize != entry.uncompressedSize) {
      error = "a stored entry has mismatched sizes";
      return false;
    }
    content.assign(source, source + entry.compressedSize);
  } else if (entry.method == 8) {
    if (!inflateRaw(source, entry.compressedSize, entry.uncompressedSize, content)) {
      error = "the compressed data could not be inflated";
      return false;
    }
  } else {
    error = "the archive uses unsupported compression method " + std::to_string(entry.method);
    return false;
  }
  const uLong checksum = crc32(0, content.data(), static_cast<uInt>(content.size()));
  if (checksum != entry.crc) {
    error = "the archive entry failed its checksum";
    return false;
  }
  return true;
}

bool holdsContent(const fs::path &path, const std::vector<std::uint8_t> &content) {
  std::error_code code;
  if (!fs::is_regular_file(path, code) || code || fs::file_size(path, code) != content.size() || code)
    return false;
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  std::vector<std::uint8_t> existing(content.size());
  input.read(reinterpret_cast<char *>(existing.data()), static_cast<std::streamsize>(existing.size()));
  return static_cast<bool>(input) && existing == content;
}

bool writeAtomically(const fs::path &target, const std::vector<std::uint8_t> &content, std::string &error) {
  std::error_code code;
  fs::create_directories(target.parent_path(), code);
  if (code) {
    error = "cannot create " + paths::toUtf8(target.parent_path());
    return false;
  }
  fs::path temporary = target;
  temporary += fs::path(".part");
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "cannot write " + paths::toUtf8(temporary);
      return false;
    }
    output.write(reinterpret_cast<const char *>(content.data()), static_cast<std::streamsize>(content.size()));
    if (!output) {
      error = "cannot write " + paths::toUtf8(temporary);
      return false;
    }
  }
  fs::rename(temporary, target, code);
  if (code) {
    fs::remove(temporary, code);
    error = "cannot place " + paths::toUtf8(target);
    return false;
  }
  return true;
}

} // namespace

bool unpackCoreFromZip(const std::string &zipPath, const std::string &expectedFileName,
                       const std::string &destinationDirectory, std::string &extractedPath, std::string &error) {
  std::vector<std::uint8_t> bytes;
  if (!readFile(zipPath, bytes, error))
    return false;
  std::size_t directoryEnd = 0;
  if (!findEndOfCentralDirectory(bytes, directoryEnd)) {
    error = zipPath + " is not a zip archive";
    return false;
  }
  const std::uint16_t count = read16(bytes.data() + directoryEnd + 10);
  const std::uint32_t directorySize = read32(bytes.data() + directoryEnd + 12);
  const std::uint32_t directoryOffset = read32(bytes.data() + directoryEnd + 16);
  if (directoryOffset == kZip64Marker || directorySize == kZip64Marker) {
    error = "zip64 archives are not supported";
    return false;
  }
  if (!inRange(directoryOffset, directorySize, bytes.size())) {
    error = "the central directory runs past the archive";
    return false;
  }
  Entry entry;
  if (!findEntry(bytes, directoryOffset, count, expectedFileName, entry, error))
    return false;
  std::vector<std::uint8_t> content;
  if (!extract(bytes, entry, content, error))
    return false;
  const fs::path target =
      paths::fromUtf8(destinationDirectory) / expectedFileName;
  // Rewriting an unchanged core would only bump its timestamp, and timestamps
  // are too coarse to tell whether the archive changed.
  if (holdsContent(target, content)) {
    extractedPath = paths::toUtf8(target);
    return true;
  }
  if (!writeAtomically(target, content, error))
    return false;
  extractedPath = paths::toUtf8(target);
  return true;
}

#else

bool unpackCoreFromZip(const std::string &, const std::string &, const std::string &, std::string &,
                       std::string &error) {
  error = "this build was compiled without zlib, so .zip cores cannot be unpacked";
  return false;
}

#endif

} // namespace launcher::runtime::libretro
