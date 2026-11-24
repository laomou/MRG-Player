#include "util/ImageFile.hpp"

#include <SDL.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

#include "util/Utf8Path.hpp"

namespace launcher::io {
namespace {

// 32 bits per pixel plus room for the headers and a palette entry set: SDL
// never writes more than that, so one allocation always covers the file.
std::size_t bmpCapacity(const SDL_Surface &surface) {
  const auto pixels = static_cast<std::size_t>(surface.w) *
                      static_cast<std::size_t>(surface.h);
  return pixels * 4U + 4096U;
}

} // namespace

bool saveSurfaceBmp(SDL_Surface *surface, const std::string &utf8Path) {
  if (surface == nullptr || surface->w <= 0 || surface->h <= 0)
    return false;
  std::vector<std::uint8_t> buffer(bmpCapacity(*surface));
  SDL_RWops *stream = SDL_RWFromMem(
      buffer.data(), static_cast<int>(buffer.size()));
  if (stream == nullptr)
    return false;
  const bool encoded = SDL_SaveBMP_RW(surface, stream, 0) == 0;
  const Sint64 written = SDL_RWtell(stream);
  SDL_RWclose(stream);
  if (!encoded || written <= 0)
    return false;
  std::ofstream output(paths::fromUtf8(utf8Path),
                       std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  output.write(reinterpret_cast<const char *>(buffer.data()),
               static_cast<std::streamsize>(written));
  return static_cast<bool>(output);
}

SDL_Surface *loadSurfaceBmp(const std::string &utf8Path) {
  std::ifstream input(paths::fromUtf8(utf8Path), std::ios::binary);
  if (!input)
    return nullptr;
  const std::vector<std::uint8_t> bytes{
      std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  if (bytes.empty())
    return nullptr;
  SDL_RWops *stream =
      SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
  if (stream == nullptr)
    return nullptr;
  return SDL_LoadBMP_RW(stream, 1);
}

} // namespace launcher::io
