#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace launcher::io {

// A decoded still image, always expanded to straight RGBA so the renderer can
// take it without caring what the file actually stored.
struct Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;
};

// Decodes a PNG or JPEG from disk. The path arrives as UTF-8 and is handed to
// the filesystem through paths::fromUtf8, so cover files with Chinese names
// open on Windows too.
[[nodiscard]] bool loadImage(const std::string &path, Image &image, std::string &error);

}  // namespace launcher::io
