#include "util/Image.hpp"

#include <fstream>
#include <iterator>

#include "util/Utf8Path.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wchar-subscripts"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace launcher::io {
namespace {

// Box art is a few hundred kilobytes; anything far larger is not a cover and
// is refused before it is decoded.
constexpr std::uintmax_t kMaximumImageBytes = 32U * 1024U * 1024U;

}  // namespace

bool loadImage(const std::string &path, Image &image, std::string &error) {
  image = {};
  std::ifstream file(paths::fromUtf8(path), std::ios::binary);
  if (!file) {
    error = "Cannot open " + path;
    return false;
  }
  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  file.seekg(0, std::ios::beg);
  if (size <= 0 || static_cast<std::uintmax_t>(size) > kMaximumImageBytes) {
    error = "The image at " + path + " is empty or too large";
    return false;
  }
  std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
  if (!file.read(reinterpret_cast<char *>(bytes.data()), size)) {
    error = "Cannot read " + path;
    return false;
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char *pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                                                &channels, 4);
  if (pixels == nullptr || width <= 0 || height <= 0) {
    const char *reason = stbi_failure_reason();
    error = "Cannot decode " + path + (reason != nullptr ? ": " + std::string(reason) : std::string());
    if (pixels != nullptr)
      stbi_image_free(pixels);
    return false;
  }
  image.width = width;
  image.height = height;
  const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
  image.pixels.assign(pixels, pixels + count);
  stbi_image_free(pixels);
  return true;
}

}  // namespace launcher::io
