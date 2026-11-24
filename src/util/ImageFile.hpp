#pragma once

#include <string>

struct SDL_Surface;

namespace launcher::io {

// SDL_SaveBMP and SDL_LoadBMP hand the file name to the C library, which reads
// it in the local code page on Windows: a game whose title is not ASCII ends up
// with a thumbnail path those helpers cannot open. These wrappers encode and
// decode the BMP in memory and move the bytes with std::filesystem, so UTF-8
// paths keep working on every platform.
[[nodiscard]] bool saveSurfaceBmp(SDL_Surface *surface,
                                  const std::string &utf8Path);
[[nodiscard]] SDL_Surface *loadSurfaceBmp(const std::string &utf8Path);

} // namespace launcher::io
