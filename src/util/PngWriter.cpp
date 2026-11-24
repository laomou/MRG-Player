#include "util/PngWriter.hpp"

#include <cstdio>
#include <filesystem>
#include <vector>

#if defined(MRG_HAVE_ZLIB)
#include <zlib.h>
#endif

#include "util/Utf8Path.hpp"

namespace launcher::io {
namespace {

#if defined(MRG_HAVE_ZLIB)
void putBigEndian(std::vector<std::uint8_t> &out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void putChunk(std::vector<std::uint8_t> &out, const char *type,
              const std::vector<std::uint8_t> &payload) {
  putBigEndian(out, static_cast<std::uint32_t>(payload.size()));
  const std::size_t crcStart = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), payload.begin(), payload.end());
  const uLong crc = crc32(crc32(0L, Z_NULL, 0), out.data() + crcStart,
                          static_cast<uInt>(out.size() - crcStart));
  putBigEndian(out, static_cast<std::uint32_t>(crc));
}
#endif

} // namespace

bool pngWritingAvailable() noexcept {
#if defined(MRG_HAVE_ZLIB)
  return true;
#else
  return false;
#endif
}

bool writePngRgba(const std::string &path, int width, int height,
                  const std::uint8_t *rgba, std::size_t pitch) noexcept {
#if !defined(MRG_HAVE_ZLIB)
  (void)path;
  (void)width;
  (void)height;
  (void)rgba;
  (void)pitch;
  return false;
#else
  if (width <= 0 || height <= 0 || rgba == nullptr ||
      pitch < static_cast<std::size_t>(width) * 4U)
    return false;

  // Each scanline is prefixed with filter type 0 (no prediction).
  std::vector<std::uint8_t> raw;
  raw.reserve(static_cast<std::size_t>(height) *
              (static_cast<std::size_t>(width) * 4U + 1U));
  for (int row = 0; row < height; ++row) {
    raw.push_back(0);
    const std::uint8_t *line = rgba + static_cast<std::size_t>(row) * pitch;
    raw.insert(raw.end(), line, line + static_cast<std::size_t>(width) * 4U);
  }

  uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::uint8_t> compressed(compressedSize);
  if (compress2(compressed.data(), &compressedSize, raw.data(),
                static_cast<uLong>(raw.size()), 6) != Z_OK)
    return false;
  compressed.resize(compressedSize);

  std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<std::uint8_t> header;
  putBigEndian(header, static_cast<std::uint32_t>(width));
  putBigEndian(header, static_cast<std::uint32_t>(height));
  header.push_back(8); // bit depth
  header.push_back(6); // colour type: RGBA
  header.push_back(0); // deflate
  header.push_back(0); // adaptive filtering
  header.push_back(0); // no interlace
  putChunk(png, "IHDR", header);
  putChunk(png, "IDAT", compressed);
  putChunk(png, "IEND", {});

#if defined(_WIN32)
  std::FILE *file = _wfopen(paths::fromUtf8(path).wstring().c_str(), L"wb");
#else
  std::FILE *file = std::fopen(path.c_str(), "wb");
#endif
  if (file == nullptr)
    return false;
  const std::size_t written = std::fwrite(png.data(), 1, png.size(), file);
  std::fclose(file);
  return written == png.size();
#endif
}

} // namespace launcher::io
