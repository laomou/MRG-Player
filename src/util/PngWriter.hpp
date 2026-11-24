#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace launcher::io {

// PNG writing is only compiled in when the platform ships zlib; callers fall
// back to a BMP dump otherwise.
[[nodiscard]] bool pngWritingAvailable() noexcept;

// Writes an 8-bit RGBA PNG (colour type 6). "rgba" must hold height rows of
// pitch bytes each, and pitch must cover width * 4 bytes. Returns false when
// PNG support is unavailable or the file could not be written.
[[nodiscard]] bool writePngRgba(const std::string &path, int width, int height,
                                const std::uint8_t *rgba,
                                std::size_t pitch) noexcept;

} // namespace launcher::io
