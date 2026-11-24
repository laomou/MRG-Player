#include "util/PngWriter.hpp"

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

std::uint32_t readBigEndian(const std::vector<std::uint8_t> &data,
                            std::size_t offset) {
  return (static_cast<std::uint32_t>(data[offset]) << 24) |
         (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
         static_cast<std::uint32_t>(data[offset + 3]);
}

} // namespace

int main() {
  using launcher::io::pngWritingAvailable;
  using launcher::io::writePngRgba;

  check(pngWritingAvailable(), "png writing is compiled in");

  // Two pixels wide and two tall so a row pitch that is not a power of two is
  // still exercised; the second row differs from the first.
  const std::uint8_t pixels[2 * 2 * 4] = {
      255, 0,   0,   255, // red
      0,   255, 0,   128, // transparent green
      0,   0,   255, 255, // blue
      255, 255, 0,   0,   // transparent yellow
  };
  const std::string directory =
      (std::filesystem::temp_directory_path() / "mrg-png-test").string();
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  const std::string path = directory + "/sample.png";
  check(writePngRgba(path, 2, 2, pixels, 2 * 4), "writePngRgba succeeds");

  std::vector<std::uint8_t> file;
  if (std::FILE *handle = std::fopen(path.c_str(), "rb")) {
    std::uint8_t buffer[512];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), handle)) > 0)
      file.insert(file.end(), buffer, buffer + read);
    std::fclose(handle);
  }
  check(file.size() > 8, "file has content");
  if (file.size() > 8) {
    const std::uint8_t signature[8] = {0x89, 'P',  'N',  'G',
                                       0x0D, 0x0A, 0x1A, 0x0A};
    check(std::memcmp(file.data(), signature, 8) == 0, "png signature");

    std::vector<std::uint8_t> idat;
    bool sawHeader = false;
    bool sawEnd = false;
    std::size_t offset = 8;
    while (offset + 8 <= file.size()) {
      const std::uint32_t length = readBigEndian(file, offset);
      const std::string type(
          reinterpret_cast<const char *>(&file[offset + 4]), 4);
      if (offset + 12 + length > file.size())
        break;
      const std::uint8_t *payload = &file[offset + 8];
      if (type == "IHDR") {
        sawHeader = true;
        check(readBigEndian(file, offset + 8) == 2, "ihdr width");
        check(readBigEndian(file, offset + 12) == 2, "ihdr height");
        check(payload[8] == 8, "ihdr bit depth");
        check(payload[9] == 6, "ihdr colour type is rgba");
      } else if (type == "IDAT") {
        idat.insert(idat.end(), payload, payload + length);
      } else if (type == "IEND") {
        sawEnd = true;
      }
      offset += 12 + length;
    }
    check(sawHeader, "ihdr chunk present");
    check(sawEnd, "iend chunk present");
    check(!idat.empty(), "idat chunk present");

    if (!idat.empty()) {
      std::vector<std::uint8_t> raw(2 * (2 * 4 + 1));
      uLongf rawSize = static_cast<uLongf>(raw.size());
      const int status = uncompress(raw.data(), &rawSize, idat.data(),
                                    static_cast<uLong>(idat.size()));
      check(status == Z_OK, "idat inflates");
      check(rawSize == raw.size(), "inflated size matches scanlines");
      if (status == Z_OK && rawSize == raw.size()) {
        check(raw[0] == 0 && raw[9] == 0, "rows use the no-filter type");
        check(std::memcmp(raw.data() + 1, pixels, 8) == 0,
              "first row round-trips");
        check(std::memcmp(raw.data() + 10, pixels + 8, 8) == 0,
              "second row round-trips");
      }
    }
  }

  std::filesystem::remove_all(directory, ec);
  if (failures == 0)
    std::printf("png writer: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
