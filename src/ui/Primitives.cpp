#include "ui/Primitives.hpp"

#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "runtime/VideoViewport.hpp"
#include "util/Utf8Path.hpp"

namespace launcher::ui {
namespace {
constexpr float kPi = 3.14159265358979323846f;
using Glyph = std::array<std::uint8_t, 7>;
std::array<TTF_Font *, 6> gFonts{};
std::string gFontPath;
std::array<TTF_Font *, 6> gLatinFonts{};
std::string gLatinFontPath;

// Canvas units to output pixels. The interface keeps its 1600x900 layout, but
// every primitive converts on the way out, so a 1920x1080 window rasterises
// text at 24 pixels instead of stretching a 20 pixel texture.
float gScale = 1.0f;
float gOffsetX = 0.0f;
float gOffsetY = 0.0f;

// The sizes the last frame reported: output is the framebuffer the widgets are
// drawn into, window is the surface SDL measures mouse positions against. On a
// HiDPI display the second is smaller than the first, and a pointer that is not
// scaled up by that ratio lands short of the control it was aimed at.
int gOutputWidth = 0;
int gOutputHeight = 0;
int gWindowWidth = 0;
int gWindowHeight = 0;

constexpr int kSubRows = 4;

float deviceX(float value) { return gOffsetX + value * gScale; }
float deviceY(float value) { return gOffsetY + value * gScale; }
float deviceLength(float value) { return value * gScale; }

void releaseGearTexture();

// Rendered text is cached by content, size, and colour: the UI draws the same
// two dozen labels every frame, and rasterising each of them through FreeType
// and uploading a fresh texture was most of the frame cost.
struct CachedText {
  SDL_Texture *texture = nullptr;
  int width = 0;
  int height = 0;
  int baseline = 0;
};
std::unordered_map<std::string, CachedText> gTextCache;
SDL_Renderer *gTextRenderer = nullptr;
constexpr std::size_t kTextCacheLimit = 512;

void clearTextCache() {
  releaseGearTexture();
  for (auto &entry : gTextCache) {
    if (entry.second.texture != nullptr)
      SDL_DestroyTexture(entry.second.texture);
  }
  gTextCache.clear();
  gTextRenderer = nullptr;
}

// Drops every rasterised font, so the next measurement opens one at the size
// the new output scale asks for. Text is cached per string, so the cache goes
// first.
void closeFonts() {
  for (TTF_Font *&font : gFonts) {
    if (font != nullptr)
      TTF_CloseFont(font);
    font = nullptr;
  }
  for (TTF_Font *&font : gLatinFonts) {
    if (font != nullptr)
      TTF_CloseFont(font);
    font = nullptr;
  }
}

// One gear silhouette, rasterised at four times the size the header asks for
// and tinted per call. Building it costs a few thousand samples, so it lives
// for as long as the renderer does.
SDL_Texture *gGearTexture = nullptr;
SDL_Renderer *gGearRenderer = nullptr;
constexpr int kGearTextureSize = 64;

void buildGearTexture(SDL_Renderer *renderer) {
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
      0, kGearTextureSize, kGearTextureSize, 32, SDL_PIXELFORMAT_RGBA32);
  if (surface == nullptr)
    return;
  auto *pixels = static_cast<std::uint32_t *>(surface->pixels);
  const float center = kGearTextureSize * 0.5f;
  const float outer = center - 1.0f;
  const float body = outer * 0.72f;
  const float hole = outer * 0.26f;
  constexpr int kTeeth = 8;
  constexpr float kToothStart = 0.32f;
  constexpr float kToothEnd = 0.68f;
  constexpr int kSamples = 4;
  for (int y = 0; y < kGearTextureSize; ++y) {
    for (int x = 0; x < kGearTextureSize; ++x) {
      int covered = 0;
      for (int sy = 0; sy < kSamples; ++sy) {
        for (int sx = 0; sx < kSamples; ++sx) {
          const float px = static_cast<float>(x) + (sx + 0.5f) / kSamples -
                           center;
          const float py = static_cast<float>(y) + (sy + 0.5f) / kSamples -
                           center;
          const float distance = std::sqrt(px * px + py * py);
          float angle = std::atan2(py, px) + kPi * 0.5f;
          if (angle < 0.0f)
            angle += 2.0f * kPi;
          const float sector = 2.0f * kPi / kTeeth;
          const float offset = std::fmod(angle, sector) / sector;
          const float limit =
              (offset > kToothStart && offset < kToothEnd) ? outer : body;
          if (distance <= limit && distance >= hole)
            ++covered;
        }
      }
      const std::uint8_t alpha = static_cast<std::uint8_t>(
          covered * 255 / (kSamples * kSamples));
      pixels[static_cast<std::size_t>(y) * (surface->pitch / 4) +
             static_cast<std::size_t>(x)] =
          (static_cast<std::uint32_t>(alpha) << 24) | 0x00FFFFFFu;
    }
  }
  gGearTexture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  if (gGearTexture != nullptr) {
    SDL_SetTextureBlendMode(gGearTexture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(gGearTexture, SDL_ScaleModeLinear);
  }
  gGearRenderer = renderer;
}

void releaseGearTexture() {
  if (gGearTexture != nullptr)
    SDL_DestroyTexture(gGearTexture);
  gGearTexture = nullptr;
  gGearRenderer = nullptr;
}

bool requiresUtf8Font(const std::string &value) {
  return std::any_of(value.begin(), value.end(),
                     [](unsigned char ch) { return ch >= 0x80; });
}

// The four sizes the interface uses, in canvas units; the output scale turns
// them into the pixel size FreeType actually rasterises.
constexpr std::array<int, 6> kFontSizes{0, 14, 20, 28, 36, 44};

int fontPixels(int scale) {
  scale = std::clamp(scale, 1, 5);
  return std::max(
      1, static_cast<int>(std::lround(kFontSizes[scale] * gScale)));
}

TTF_Font *fontForScale(int scale, bool latin = false) {
  scale = std::clamp(scale, 1, 5);
  TTF_Font *&font =
      (latin ? gLatinFonts : gFonts)[static_cast<std::size_t>(scale)];
  const auto &path = latin ? gLatinFontPath : gFontPath;
  if (font == nullptr && !path.empty()) {
    font = TTF_OpenFont(path.c_str(), fontPixels(scale));
    if (font != nullptr)
      TTF_SetFontHinting(font, TTF_HINTING_NORMAL);
  }
  return font;
}

void setColor(SDL_Renderer *renderer, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

// Draws one horizontal span of device pixels. The part that covers whole pixels
// becomes a single rect and the two ends get the leftover coverage as alpha,
// which is what smooths a curve that lands between pixels.
void span(SDL_Renderer *renderer, float left, float right, float y,
          SDL_Color color) {
  if (right <= left)
    return;
  const float firstFull = std::ceil(left);
  const float lastFull = std::floor(right);
  if (lastFull <= firstFull) {
    const int pixel = static_cast<int>(std::floor(left));
    SDL_Color shade = color;
    shade.a = static_cast<Uint8>(
        std::lround(static_cast<float>(color.a) * (right - left)));
    if (shade.a == 0)
      return;
    setColor(renderer, shade);
    const SDL_FRect box{static_cast<float>(pixel), y, 1.0f, 1.0f};
    SDL_RenderFillRectF(renderer, &box);
    return;
  }
  if (firstFull > left) {
    const int pixel = static_cast<int>(std::floor(left));
    SDL_Color shade = color;
    shade.a = static_cast<Uint8>(std::lround(
        static_cast<float>(color.a) * (firstFull - left)));
    if (shade.a != 0) {
      setColor(renderer, shade);
      const SDL_FRect box{static_cast<float>(pixel), y, 1.0f, 1.0f};
      SDL_RenderFillRectF(renderer, &box);
    }
  }
  setColor(renderer, color);
  const SDL_FRect middle{firstFull, y, lastFull - firstFull, 1.0f};
  SDL_RenderFillRectF(renderer, &middle);
  if (right > lastFull) {
    SDL_Color shade = color;
    shade.a = static_cast<Uint8>(
        std::lround(static_cast<float>(color.a) * (right - lastFull)));
    if (shade.a != 0) {
      setColor(renderer, shade);
      const SDL_FRect box{lastFull, y, 1.0f, 1.0f};
      SDL_RenderFillRectF(renderer, &box);
    }
  }
}

// Fills a rectangle that is already expressed in device pixels; the drawing
// helpers that walk the output row by row use this so their rows stay one
// pixel tall no matter how the canvas is scaled.
void fillDevice(SDL_Renderer *renderer, float x, float y, float width,
                float height, SDL_Color color) {
  setColor(renderer, color);
  const SDL_FRect rect{x, y, width, height};
  SDL_RenderFillRectF(renderer, &rect);
}

Glyph glyph(char ch) {
  switch (ch) {
  case 'A':
    return {14, 17, 17, 31, 17, 17, 17};
  case 'B':
    return {30, 17, 17, 30, 17, 17, 30};
  case 'C':
    return {14, 17, 16, 16, 16, 17, 14};
  case 'D':
    return {30, 17, 17, 17, 17, 17, 30};
  case 'E':
    return {31, 16, 16, 30, 16, 16, 31};
  case 'F':
    return {31, 16, 16, 30, 16, 16, 16};
  case 'G':
    return {14, 17, 16, 23, 17, 17, 15};
  case 'H':
    return {17, 17, 17, 31, 17, 17, 17};
  case 'I':
    return {31, 4, 4, 4, 4, 4, 31};
  case 'J':
    return {7, 2, 2, 2, 18, 18, 12};
  case 'K':
    return {17, 18, 20, 24, 20, 18, 17};
  case 'L':
    return {16, 16, 16, 16, 16, 16, 31};
  case 'M':
    return {17, 27, 21, 21, 17, 17, 17};
  case 'N':
    return {17, 25, 21, 19, 17, 17, 17};
  case 'O':
    return {14, 17, 17, 17, 17, 17, 14};
  case 'P':
    return {30, 17, 17, 30, 16, 16, 16};
  case 'Q':
    return {14, 17, 17, 17, 21, 18, 13};
  case 'R':
    return {30, 17, 17, 30, 20, 18, 17};
  case 'S':
    return {15, 16, 16, 14, 1, 1, 30};
  case 'T':
    return {31, 4, 4, 4, 4, 4, 4};
  case 'U':
    return {17, 17, 17, 17, 17, 17, 14};
  case 'V':
    return {17, 17, 17, 17, 17, 10, 4};
  case 'W':
    return {17, 17, 17, 21, 21, 21, 10};
  case 'X':
    return {17, 17, 10, 4, 10, 17, 17};
  case 'Y':
    return {17, 17, 10, 4, 4, 4, 4};
  case 'Z':
    return {31, 1, 2, 4, 8, 16, 31};
  case '0':
    return {14, 17, 19, 21, 25, 17, 14};
  case '1':
    return {4, 12, 4, 4, 4, 4, 14};
  case '2':
    return {14, 17, 1, 2, 4, 8, 31};
  case '3':
    return {30, 1, 1, 14, 1, 1, 30};
  case '4':
    return {2, 6, 10, 18, 31, 2, 2};
  case '5':
    return {31, 16, 16, 30, 1, 1, 30};
  case '6':
    return {14, 16, 16, 30, 17, 17, 14};
  case '7':
    return {31, 1, 2, 4, 8, 8, 8};
  case '8':
    return {14, 17, 17, 14, 17, 17, 14};
  case '9':
    return {14, 17, 17, 15, 1, 1, 14};
  case '-':
    return {0, 0, 0, 31, 0, 0, 0};
  case '/':
    return {1, 2, 2, 4, 8, 8, 16};
  case '.':
    return {0, 0, 0, 0, 0, 12, 12};
  case ':':
    return {0, 12, 12, 0, 12, 12, 0};
  case '+':
    return {0, 4, 4, 31, 4, 4, 0};
  case '?':
    return {14, 17, 1, 2, 4, 0, 4};
  case '<':
    return {2, 4, 8, 16, 8, 4, 2};
  case '>':
    return {8, 4, 2, 1, 2, 4, 8};
  default:
    return {0, 0, 0, 0, 0, 0, 0};
  }
}
} // namespace

bool initializeTextRenderer(const std::string &fontPath) {
  if (TTF_WasInit() == 0 && TTF_Init() != 0)
    return false;
  gFontPath = fontPath;
  gLatinFontPath = paths::toUtf8(
      paths::fromUtf8(fontPath).parent_path() / "DejaVuSans.ttf");
  return fontForScale(2) != nullptr;
}

void shutdownTextRenderer() {
  clearTextCache();
  closeFonts();
  gLatinFontPath.clear();
  gFontPath.clear();
  if (TTF_WasInit() != 0)
    TTF_Quit();
}

void setClipRect(SDL_Renderer *renderer, float x, float y, float width,
                 float height) {
  const SDL_Rect clip{
      static_cast<int>(std::lround(deviceX(x))),
      static_cast<int>(std::lround(deviceY(y))),
      std::max(0, static_cast<int>(std::lround(deviceLength(width)))),
      std::max(0, static_cast<int>(std::lround(deviceLength(height))))};
  SDL_RenderSetClipRect(renderer, &clip);
}

void clearClipRect(SDL_Renderer *renderer) {
  SDL_RenderSetClipRect(renderer, nullptr);
}

SDL_Color mix(SDL_Color a, SDL_Color b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return {static_cast<Uint8>(a.r + (b.r - a.r) * t),
          static_cast<Uint8>(a.g + (b.g - a.g) * t),
          static_cast<Uint8>(a.b + (b.b - a.b) * t),
          static_cast<Uint8>(a.a + (b.a - a.a) * t)};
}

void fill(SDL_Renderer *renderer, float x, float y, float width, float height,
          SDL_Color color) {
  setColor(renderer, color);
  const SDL_FRect rect{deviceX(x), deviceY(y), deviceLength(width),
                       deviceLength(height)};
  SDL_RenderFillRectF(renderer, &rect);
}

void drawTexture(SDL_Renderer *renderer, SDL_Texture *texture, float x, float y,
                 float width, float height) {
  if (texture == nullptr)
    return;
  const SDL_FRect box{deviceX(x), deviceY(y), deviceLength(width),
                      deviceLength(height)};
  SDL_RenderCopyF(renderer, texture, nullptr, &box);
}

void gearIcon(SDL_Renderer *renderer, float centerX, float centerY,
              float radius, SDL_Color color) {
  if (renderer == nullptr || radius <= 0.0f)
    return;
  if (gGearTexture == nullptr || gGearRenderer != renderer) {
    releaseGearTexture();
    buildGearTexture(renderer);
  }
  if (gGearTexture == nullptr)
    return;
  SDL_SetTextureColorMod(gGearTexture, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(gGearTexture, color.a);
  const float cx = deviceX(centerX);
  const float cy = deviceY(centerY);
  const float size = deviceLength(radius);
  const SDL_FRect box{cx - size, cy - size, size * 2.0f, size * 2.0f};
  SDL_RenderCopyF(renderer, gGearTexture, nullptr, &box);
  SDL_SetTextureColorMod(gGearTexture, 255, 255, 255);
  SDL_SetTextureAlphaMod(gGearTexture, 255);
}

void circle(SDL_Renderer *renderer, int centerX, int centerY, int radius,
            SDL_Color color) {
  const float cx = deviceX(static_cast<float>(centerX));
  const float cy = deviceY(static_cast<float>(centerY));
  const float r = deviceLength(static_cast<float>(radius));
  if (r <= 0.0f)
    return;
  const int firstRow = static_cast<int>(std::lround(cy - r));
  const int rows = std::max(1, static_cast<int>(std::lround(r * 2.0f)));
  // Vertical coverage is sampled four times per row and then applied as one
  // alpha, so the edge is soft without the row being blended four times over.
  for (int row = 0; row < rows; ++row) {
    int inside = 0;
    float widest = 0.0f;
    for (int part = 0; part < kSubRows; ++part) {
      const float offset =
          static_cast<float>(row) + (part + 0.5f) / kSubRows - r;
      const float half = std::sqrt(std::max(0.0f, r * r - offset * offset));
      if (half > 0.0f) {
        ++inside;
        widest = std::max(widest, half);
      }
    }
    if (inside == 0)
      continue;
    SDL_Color shade = color;
    shade.a = static_cast<Uint8>(std::lround(
        static_cast<float>(color.a) * inside / kSubRows));
    span(renderer, cx - widest, cx + widest,
         static_cast<float>(firstRow + row), shade);
  }
}

void rounded(SDL_Renderer *renderer, float x, float y, float width,
             float height, float radius, SDL_Color color) {
  if (width <= 0 || height <= 0)
    return;
  radius = std::clamp(radius, 0.0f, std::min(width, height) * 0.5f);
  const float left = deviceX(x);
  const float top = deviceY(y);
  const float w = deviceLength(width);
  const float h = deviceLength(height);
  const float r = deviceLength(radius);
  const int firstRow = static_cast<int>(std::lround(top));
  const int rows = std::max(1, static_cast<int>(std::lround(h)));
  // Four vertical slices per row: the corner curve is sampled in both
  // directions, then the row is drawn once with the coverage as its alpha so
  // the panel is blended exactly once.
  for (int row = 0; row < rows; ++row) {
    int inside = 0;
    float insetSum = 0.0f;
    for (int part = 0; part < kSubRows; ++part) {
      const float yy = static_cast<float>(row) + (part + 0.5f) / kSubRows;
      if (yy <= 0.0f || yy >= h)
        continue;
      const float distance = std::min(yy, h - yy);
      const float curve = std::max(0.0f, r - distance);
      insetSum += r - std::sqrt(std::max(0.0f, r * r - curve * curve));
      ++inside;
    }
    if (inside == 0)
      continue;
    const float inset = insetSum / static_cast<float>(inside);
    SDL_Color shade = color;
    shade.a = static_cast<Uint8>(std::lround(
        static_cast<float>(color.a) * inside / kSubRows));
    span(renderer, left + inset, left + w - inset,
         static_cast<float>(firstRow + row), shade);
  }
}

void gradientRounded(SDL_Renderer *renderer, float x, float y, float width,
                     float height, float radius, SDL_Color top,
                     SDL_Color bottom) {
  const float left = deviceX(x);
  const float first = deviceY(y);
  const float w = deviceLength(width);
  const float h = deviceLength(height);
  const float r = deviceLength(radius);
  const int rows = std::max(1, static_cast<int>(std::lround(h)));
  for (int row = 0; row < rows; ++row) {
    const float t = (static_cast<float>(row) + 0.5f) / static_cast<float>(rows);
    const float distance =
        std::min(static_cast<float>(row) + 0.5f, h - row - 0.5f);
    const float yy = std::max(0.0f, r - distance);
    const float inset = r - std::sqrt(std::max(0.0f, r * r - yy * yy));
    span(renderer, left + inset, left + w - inset, first + row,
         mix(top, bottom, t));
  }
}

void fadeBottomRounded(SDL_Renderer *renderer, float x, float y, float width,
                       float height, float radius, SDL_Color color,
                       Uint8 strength, float exponent) {
  const float left = deviceX(x);
  const float first = deviceY(y);
  const float w = deviceLength(width);
  const float h = deviceLength(height);
  const float r = deviceLength(radius);
  const int rows = std::max(1, static_cast<int>(std::lround(h)));
  for (int row = 0; row < rows; ++row) {
    const float t = (static_cast<float>(row) + 0.5f) / static_cast<float>(rows);
    const float distance = h - row - 0.5f;
    const float yy = std::max(0.0f, r - distance);
    const float inset = r - std::sqrt(std::max(0.0f, r * r - yy * yy));
    const float shade =
        static_cast<float>(strength) * std::pow(std::min(1.0f, t), exponent);
    SDL_Color rowColor = color;
    rowColor.a = static_cast<Uint8>(std::lround(shade));
    if (rowColor.a == 0)
      continue;
    span(renderer, left + inset, left + w - inset, first + row, rowColor);
  }
}

// The fallback face is drawn in device pixels as well, so it keeps the same
// size as the rasterised fonts it stands in for.
int bitmapTextAdvance(const std::string &value, int scale) {
  const int pixel = std::max(1, static_cast<int>(std::lround(scale * gScale)));
  return static_cast<int>(value.size()) * 6 * pixel;
}

void drawBitmapText(SDL_Renderer *renderer, int x, int y,
                    const std::string &value, int scale, SDL_Color color) {
  setColor(renderer, color);
  const int pixel = std::max(1, static_cast<int>(std::lround(scale * gScale)));
  int cursor = x;
  for (char raw : value) {
    const char ch =
        (raw >= 'a' && raw <= 'z') ? static_cast<char>(raw - 'a' + 'A') : raw;
    const Glyph bits = glyph(ch);
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if ((bits[row] & (1u << (4 - col))) != 0u) {
          const SDL_Rect box{cursor + col * pixel, y + row * pixel, pixel,
                             pixel};
          SDL_RenderFillRect(renderer, &box);
        }
      }
    }
    cursor += 6 * pixel;
  }
}

int utf8TextWidth(const std::string &value, int scale) {
  int width = 0;
  int height = 0;
  TTF_Font *font = fontForScale(scale, !requiresUtf8Font(value));
  if (font == nullptr ||
      TTF_SizeUTF8(font, value.c_str(), &width, &height) != 0) {
    return bitmapTextAdvance(value, scale);
  }
  return width;
}

int textWidth(const std::string &value, int scale) {
  if (value.empty())
    return 0;

  int width = 0;
  std::size_t begin = 0;
  while (begin < value.size()) {
    const bool utf8 = static_cast<unsigned char>(value[begin]) >= 0x80;
    std::size_t end = begin + 1;
    while (end < value.size() &&
           (static_cast<unsigned char>(value[end]) >= 0x80) == utf8) {
      ++end;
    }
    const std::string run = value.substr(begin, end - begin);
    width += utf8TextWidth(run, scale);
    begin = end;
  }
  return gScale > 0.0f ? static_cast<int>(std::lround(width / gScale)) : width;
}

void textDevice(SDL_Renderer *renderer, int x, int y,
                const std::string &value, int scale, SDL_Color color) {
  int cursor = x;
  std::size_t begin = 0;
  while (begin < value.size()) {
    const bool utf8 = static_cast<unsigned char>(value[begin]) >= 0x80;
    std::size_t end = begin + 1;
    while (end < value.size() &&
           (static_cast<unsigned char>(value[end]) >= 0x80) == utf8) {
      ++end;
    }
    const std::string run = value.substr(begin, end - begin);
    TTF_Font *font = fontForScale(scale, !utf8);
    if (font == nullptr) {
      drawBitmapText(renderer, cursor, y, run, scale, color);
      cursor += bitmapTextAdvance(run, scale);
      begin = end;
      continue;
    }
    if (gTextRenderer != renderer)
      clearTextCache();
    gTextRenderer = renderer;
    char keySuffix[40];
    std::snprintf(keySuffix, sizeof(keySuffix), "\x1f%d|%d|%u,%u,%u,%u",
                  scale, fontPixels(scale),
                  static_cast<unsigned>(color.r),
                  static_cast<unsigned>(color.g),
                  static_cast<unsigned>(color.b),
                  static_cast<unsigned>(color.a));
    std::string key = run;
    key += keySuffix;
    const auto cached = gTextCache.find(key);
    if (cached != gTextCache.end()) {
      const CachedText &entry = cached->second;
      const SDL_Rect destination{
          cursor, y + entry.baseline - TTF_FontAscent(font), entry.width,
          entry.height};
      SDL_RenderCopy(renderer, entry.texture, nullptr, &destination);
      cursor += entry.width;
      begin = end;
      continue;
    }
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, run.c_str(), color);
    if (surface != nullptr) {
      SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
      if (texture != nullptr) {
        // Align mixed Latin/CJK runs on a shared baseline.
        TTF_Font *cjk = fontForScale(scale);
        const int baseline = cjk ? TTF_FontAscent(cjk) : TTF_FontAscent(font);
        const SDL_Rect destination{cursor, y + baseline - TTF_FontAscent(font),
                                   surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
        if (gTextCache.size() >= kTextCacheLimit)
          clearTextCache();
        gTextCache[key] = CachedText{texture, surface->w, surface->h, baseline};
      }
      cursor += surface->w;
      SDL_FreeSurface(surface);
    }
    begin = end;
  }
}

void text(SDL_Renderer *renderer, int x, int y, const std::string &value,
          int scale, SDL_Color color) {
  textDevice(renderer, static_cast<int>(std::lround(deviceX(x))),
             static_cast<int>(std::lround(deviceY(y))), value, scale, color);
}

void centeredText(SDL_Renderer *renderer, int centerX, int y,
                  const std::string &value, int scale, SDL_Color color) {
  text(renderer, centerX - textWidth(value, scale) / 2, y, value, scale, color);
}

void centeredGlyph(SDL_Renderer *renderer, int centerX, int centerY,
                   const std::string &value, int scale, SDL_Color color) {
  if (value.empty())
    return;
  const int cx = static_cast<int>(std::lround(deviceX(centerX)));
  const int cy = static_cast<int>(std::lround(deviceY(centerY)));
  TTF_Font *font = fontForScale(scale, !requiresUtf8Font(value));
  int width = 0;
  int height = 0;
  if (font == nullptr ||
      TTF_SizeUTF8(font, value.c_str(), &width, &height) != 0) {
    textDevice(renderer, cx - bitmapTextAdvance(value, scale) / 2,
               cy - static_cast<int>(std::lround(3 * scale * gScale)), value,
               scale, color);
    return;
  }
  TTF_Font *cjk = fontForScale(scale);
  const int baseline = cjk ? TTF_FontAscent(cjk) : TTF_FontAscent(font);
  const int top = cy - height / 2 - (baseline - TTF_FontAscent(font));
  textDevice(renderer, cx - width / 2, top, value, scale, color);
}

void setOutputSize(int outputWidth, int outputHeight) {
  gOutputWidth = outputWidth;
  gOutputHeight = outputHeight;
  const runtime::CanvasTransform frame =
      runtime::fitCanvas(outputWidth, outputHeight);
  const float delta = std::abs(frame.scale - gScale);
  const bool moved = std::abs(frame.offsetX - gOffsetX) >= 0.5f ||
                     std::abs(frame.offsetY - gOffsetY) >= 0.5f;
  if (delta < 0.0005f && !moved)
    return;
  gScale = frame.scale;
  gOffsetX = frame.offsetX;
  gOffsetY = frame.offsetY;
  if (delta < 0.0005f)
    return;
  // A new scale means a new rasterisation size for every glyph.
  clearTextCache();
  closeFonts();
}

void setWindowSize(int windowWidth, int windowHeight) {
  gWindowWidth = windowWidth;
  gWindowHeight = windowHeight;
}

void windowToCanvas(float windowX, float windowY, float &canvasX,
                    float &canvasY) {
  if (gScale <= 0.0f) {
    canvasX = windowX;
    canvasY = windowY;
    return;
  }
  // An event speaks window points; the canvas transform speaks framebuffer
  // pixels. Scaling first keeps a 2x display from halving every click.
  const float pixelX = gWindowWidth > 0 && gOutputWidth > 0
                           ? static_cast<float>(gOutputWidth) /
                                 static_cast<float>(gWindowWidth)
                           : 1.0f;
  const float pixelY = gWindowHeight > 0 && gOutputHeight > 0
                           ? static_cast<float>(gOutputHeight) /
                                 static_cast<float>(gWindowHeight)
                           : 1.0f;
  canvasX = (windowX * pixelX - gOffsetX) / gScale;
  canvasY = (windowY * pixelY - gOffsetY) / gScale;
}

void canvasRect(float &x, float &y, float &width, float &height) {
  x = gOffsetX;
  y = gOffsetY;
  width = deviceLength(kLogicalWidth);
  height = deviceLength(kLogicalHeight);
}

void drawBackground(SDL_Renderer *renderer, const Game &game, float time,
                    SDL_Texture *wallpaper) {
  // Every band here is walked in device rows, so the gradient is smooth at any
  // window size instead of being stretched by the renderer.
  const float left = gOffsetX;
  const float top = gOffsetY;
  const float width = deviceLength(kLogicalWidth);
  const float height = deviceLength(kLogicalHeight);
  const int rows = std::max(1, static_cast<int>(std::lround(height)));
  if (wallpaper != nullptr) {
    int textureWidth = 0;
    int textureHeight = 0;
    if (SDL_QueryTexture(wallpaper, nullptr, nullptr, &textureWidth,
                         &textureHeight) == 0 &&
        textureWidth > 0 && textureHeight > 0) {
      // Fill the canvas and let the extra edges fall outside it.
      const float artScale =
          std::max(width / textureWidth, height / textureHeight);
      const float artWidth = textureWidth * artScale;
      const float artHeight = textureHeight * artScale;
      const SDL_FRect frame{left + (width - artWidth) * 0.5f,
                            top + (height - artHeight) * 0.5f, artWidth,
                            artHeight};
      SDL_RenderCopyF(renderer, wallpaper, nullptr, &frame);
      for (int row = 0; row < rows; ++row) {
        const float t = static_cast<float>(row) / rows;
        const Uint8 alpha = static_cast<Uint8>(202 + 42 * t);
        fillDevice(renderer, left, top + row, width, 1.0f, {4, 8, 20, alpha});
      }
    }
  } else {
  for (int row = 0; row < rows; ++row) {
    const float t = static_cast<float>(row) / rows;
    setColor(renderer, mix(game.top, game.bottom, t));
    SDL_RenderDrawLineF(renderer, left, top + row + 0.5f, left + width,
                        top + row + 0.5f);
  }
  const int sunX = 1260 + static_cast<int>(std::sin(time * 0.08f) * 22.0f);
  circle(renderer, sunX, 172, 90,
         {game.accent.r, game.accent.g, game.accent.b, 150});
  setColor(renderer, {15, 27, 57, 175});
  std::array<SDL_FPoint, 13> far{{{deviceX(0.0f), deviceY(520.0f)},
                                 {deviceX(150.0f), deviceY(350.0f)},
                                 {deviceX(270.0f), deviceY(455.0f)},
                                 {deviceX(420.0f), deviceY(275.0f)},
                                 {deviceX(565.0f), deviceY(475.0f)},
                                 {deviceX(725.0f), deviceY(325.0f)},
                                 {deviceX(900.0f), deviceY(485.0f)},
                                 {deviceX(1060.0f), deviceY(300.0f)},
                                 {deviceX(1220.0f), deviceY(465.0f)},
                                 {deviceX(1405.0f), deviceY(335.0f)},
                                 {deviceX(1600.0f), deviceY(475.0f)},
                                 {deviceX(1600.0f), deviceY(900.0f)},
                                 {deviceX(0.0f), deviceY(900.0f)}}};
  SDL_RenderDrawLinesF(renderer, far.data(), 11);
  const float waveTop = deviceY(520.0f);
  for (int row = 0; row < rows; ++row) {
    const float y = top + row;
    if (y < waveTop)
      continue;
    const float logical = (y - top) / gScale;
    const float wave = 70.0f * std::sin((logical - 500.0f) * 0.013f);
    const int shade =
        std::clamp(135 + (static_cast<int>(logical) - 520) / 2, 135, 180);
    fillDevice(renderer, left, y, width, 1.0f,
               {12, 22, 48, static_cast<Uint8>(shade)});
    if ((static_cast<int>(logical) % 34) == 0) {
      fillDevice(renderer, left, y, deviceLength(770.0f + wave), 1.0f,
                 {game.accent.r, game.accent.g, game.accent.b, 22});
    }
  }
  fill(renderer, 82, 548, 315, 40, {150, 67, 61, 120});
  fill(renderer, 82, 588, 315, 17, {85, 47, 54, 150});
  fill(renderer, 1110, 510, 390, 40, {150, 67, 61, 110});
  for (int i = 0; i < 3; ++i)
    fill(renderer, 198.0f + i * 52.0f, 454, 48, 48,
         {game.accent.r, game.accent.g, game.accent.b, 135});
  for (int row = 0; row < rows; ++row) {
    const float t = static_cast<float>(row) / rows;
    const Uint8 alpha = static_cast<Uint8>(18 + 185 * t * t);
    fillDevice(renderer, left, top + row, width, 1.0f, {4, 8, 20, alpha});
  }
  const float scanStep = deviceLength(4.0f);
  const float scanHeight = std::max(1.0f, deviceLength(2.0f));
  for (float y = top; y < top + height; y += scanStep)
    fillDevice(renderer, left, y, width, scanHeight,
         {255, 255, 255, 4});
  }
}

namespace {
// Footer legend metrics. The chip is deliberately the same height as a scale 1
// line of text plus padding so a labelled hint sits on the footer baseline.
constexpr float kHintChipPadding = 9.0f;
constexpr float kHintMinChipWidth = 26.0f;
constexpr float kHintLabelGap = 10.0f;

[[nodiscard]] float chipWidth(const Hint &hint) {
  return std::max(kHintMinChipWidth,
                  static_cast<float>(textWidth(hint.key, 1)) +
                      kHintChipPadding * 2.0f);
}
} // namespace

int hintWidth(const Hint &hint) {
  return static_cast<int>(chipWidth(hint)) + static_cast<int>(kHintLabelGap) +
         textWidth(hint.label, 1);
}

int hintRowWidth(const std::vector<Hint> &hints) {
  int total = 0;
  for (std::size_t index = 0; index < hints.size(); ++index) {
    if (index != 0)
      total += static_cast<int>(kHintGap);
    total += hintWidth(hints[index]);
  }
  return total;
}

void drawHint(SDL_Renderer *renderer, float x, float y, const Hint &hint,
              SDL_Color accent) {
  // Button first, then what it does: the eye lands on the control and reads the
  // action to its right, the way a console legend is written.
  const float chipX = x;
  const float width = chipWidth(hint);
  // Scale-one runs land their ink centre three pixels below the top of a
  // 26 pixel chip when drawn from y + 6, so the row is lifted to sit on the
  // chip's middle instead.
  const int textY = static_cast<int>(y) + 3;
  text(renderer, static_cast<int>(chipX + width + kHintLabelGap), textY,
       hint.label, 1, {143, 163, 181, 255});
  rounded(renderer, chipX, y, width, kHintChipHeight, 8,
          hint.accent ? accent : SDL_Color{255, 255, 255, 26});
  centeredText(renderer, static_cast<int>(chipX + width * 0.5f), textY,
               hint.key, 1,
               hint.accent ? SDL_Color{20, 24, 35, 255}
                           : SDL_Color{232, 238, 247, 255});
}

void drawHintRow(SDL_Renderer *renderer, float rightX, float y,
                 const std::vector<Hint> &hints, SDL_Color accent) {
  float x = rightX - static_cast<float>(hintRowWidth(hints));
  for (const Hint &hint : hints) {
    drawHint(renderer, x, y, hint, accent);
    x += static_cast<float>(hintWidth(hint)) + kHintGap;
  }
}

namespace {
SDL_Color focusColor(float opacity) {
  const float clamped = std::clamp(opacity, 0.0f, 1.0f);
  return {kFocusColor.r, kFocusColor.g, kFocusColor.b,
          static_cast<Uint8>(std::lround(clamped * 255.0f))};
}

// Paints only the border of a rounded rectangle, so a ring can sit outside a
// control without repainting the control or whatever shows through it. Each
// row contributes a left and a right span once it is far enough from the
// corners to have an inside.
void roundedBand(SDL_Renderer *renderer, float x, float y, float width,
                 float height, float radius, float thickness,
                 SDL_Color color) {
  const float left = deviceX(x);
  const float top = deviceY(y);
  const float boxWidth = deviceLength(width);
  const float boxHeight = deviceLength(height);
  radius = deviceLength(radius);
  radius = std::clamp(radius, 0.0f, std::min(boxWidth, boxHeight) * 0.5f);
  thickness = std::clamp(deviceLength(thickness), 1.0f,
                         std::min(boxWidth, boxHeight) * 0.5f);
  const float inner = std::max(0.0f, radius - thickness);
  const int rows = std::max(1, static_cast<int>(std::lround(boxHeight)));
  for (int row = 0; row < rows; ++row) {
    const float distance =
        std::min(static_cast<float>(row) + 0.5f,
                 boxHeight - static_cast<float>(row) - 0.5f);
    const float line = top + row;
    const float outerY = std::max(0.0f, radius - distance);
    const float outerInset =
        radius - std::sqrt(std::max(0.0f, radius * radius - outerY * outerY));
    if (distance < thickness) {
      span(renderer, left + outerInset, left + boxWidth - outerInset, line,
           color);
      continue;
    }
    const float innerY = std::max(0.0f, inner - (distance - thickness));
    const float innerInset =
        inner - std::sqrt(std::max(0.0f, inner * inner - innerY * innerY));
    const float leftEdge = left + outerInset;
    const float leftEnd = left + thickness + innerInset;
    if (leftEnd > leftEdge)
      span(renderer, leftEdge, leftEnd, line, color);
    const float rightEdge = left + boxWidth - outerInset;
    const float rightStart = left + boxWidth - thickness - innerInset;
    if (rightEdge > rightStart)
      span(renderer, rightStart, rightEdge, line, color);
  }
}
} // namespace

void drawFocusRing(SDL_Renderer *renderer, float x, float y, float width,
                   float height, float radius, float opacity) {
  if (opacity <= 0.0f)
    return;
  // Three pixels clear of the control, two pixels thick, no halo: a line that
  // says where the keys are without re-reading as a filled plate.
  roundedBand(renderer, x - 3.0f, y - 3.0f, width + 6.0f, height + 6.0f,
              radius + 3.0f, 2.0f, focusColor(opacity));
}

void drawHintFocus(SDL_Renderer *renderer, float x, float y, const Hint &hint,
                   float opacity) {
  if (opacity <= 0.0f)
    return;
  const float width = static_cast<float>(hintWidth(hint));
  roundedBand(renderer, x - 9.0f, y - 6.0f, width + 18.0f,
              kHintChipHeight + 12.0f, 11.0f, 2.0f, focusColor(opacity));
}

} // namespace launcher::ui
