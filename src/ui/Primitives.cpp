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

// Native 1280x720 canvas units to output pixels. Every primitive converts on
// the way out, so larger windows rasterise text at the target resolution
// instead of stretching a lower-resolution texture.
float gScale = 1.0f;
float gOffsetX = 0.0f;
float gOffsetY = 0.0f;
// Shift applied while a layer is baked into a texture, so the shape lands at
// the texture origin without the caller having to move it.
int gBakeShiftX = 0;
int gBakeShiftY = 0;

// The sizes the last frame reported: output is the framebuffer the widgets are
// drawn into, window is the surface SDL measures mouse positions against. On a
// HiDPI display the second is smaller than the first, and a pointer that is not
// scaled up by that ratio lands short of the control it was aimed at.
int gOutputWidth = 0;
int gOutputHeight = 0;
int gWindowWidth = 0;
int gWindowHeight = 0;

constexpr int kSubRows = 4;

float deviceX(float value) {
  return gOffsetX + value * gScale - static_cast<float>(gBakeShiftX);
}
float deviceY(float value) {
  return gOffsetY + value * gScale - static_cast<float>(gBakeShiftY);
}
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
// A baked copy of the backdrop, keyed by everything that changes how it looks.
// Rebuilt only when the size, the theme, the wallpaper, or the sun's rounded
// position moves.
struct Backdrop {
  SDL_Texture *texture = nullptr;
  SDL_Renderer *renderer = nullptr;
  int width = 0;
  int height = 0;
  SDL_Color top{};
  SDL_Color bottom{};
  SDL_Color accent{};
  SDL_Texture *wallpaper = nullptr;
  int sunX = 0;
  // Set when the backend refuses to render into a texture: paint directly
  // instead of paying for a failed attempt every frame.
  bool unwritable = false;
};
Backdrop gBackdrop;

void releaseBackdrop() {
  if (gBackdrop.texture != nullptr)
    SDL_DestroyTexture(gBackdrop.texture);
  gBackdrop = Backdrop{};
}

// A card is three shapes stacked: the panel, the shade the title sits on and,
// for an empty slot, two rounded plates. They are identical every frame and
// only move when the shelf travels, so each one is rasterised once into a
// texture and copied afterwards. A card went from a few hundred one-row fills
// to one copy per layer.
enum class LayerKind { Panel, Slot, Scrim };

struct Layer {
  SDL_Texture *texture = nullptr;
  SDL_Renderer *renderer = nullptr;
  LayerKind kind = LayerKind::Panel;
  int width = 0;
  int height = 0;
  // Sub-pixel part of the origin, in 1/256 of a pixel: two cards at the same
  // fraction share a texture even when their whole-pixel origins differ.
  int offsetX = 0;
  int offsetY = 0;
  int radius = 0;
  int strength = 0;
  Uint32 color = 0;
  Uint32 inner = 0;
  int exponent = 0;
  Uint64 used = 0;
};

std::vector<Layer> gLayers;
constexpr std::size_t kLayerLimit = 16;
Uint64 gLayerClock = 0;

void releaseLayers() {
  for (Layer &layer : gLayers) {
    if (layer.texture != nullptr)
      SDL_DestroyTexture(layer.texture);
  }
  gLayers.clear();
}

Uint32 packColor(SDL_Color color) {
  return (static_cast<Uint32>(color.r) << 24) |
         (static_cast<Uint32>(color.g) << 16) |
         (static_cast<Uint32>(color.b) << 8) | color.a;
}

struct LayerSpec {
  LayerKind kind = LayerKind::Panel;
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float radius = 0.0f;
  SDL_Color color{};
  SDL_Color inner{};
  Uint8 strength = 0;
  float exponent = 1.0f;
};

void paintLayer(SDL_Renderer *renderer, const LayerSpec &spec) {
  switch (spec.kind) {
  case LayerKind::Panel:
    gradientRounded(renderer, spec.x, spec.y, spec.width, spec.height,
                    spec.radius, spec.color, spec.inner);
    break;
  case LayerKind::Slot:
    rounded(renderer, spec.x, spec.y, spec.width, spec.height, spec.radius,
            spec.color);
    rounded(renderer, spec.x + 1.0f, spec.y + 1.0f, spec.width - 2.0f,
            spec.height - 2.0f, spec.radius - 1.0f, spec.inner);
    break;
  case LayerKind::Scrim:
    fadeBottomRounded(renderer, spec.x, spec.y, spec.width, spec.height,
                      spec.radius, spec.color, spec.strength, spec.exponent);
    break;
  }
}

// Bakes the layer and returns its cache entry, or nullptr when the backend
// cannot render into a texture. Everything is painted with blending off so the
// texture keeps the exact per-pixel alpha the direct draw would have produced;
// the copy then blends it onto the frame the way the spans would have.
const Layer *layerTexture(SDL_Renderer *renderer, const LayerSpec &spec,
                          int originX, int originY) {
  const int width =
      std::max(1, static_cast<int>(std::ceil(deviceLength(spec.width))) + 1);
  const int height =
      std::max(1, static_cast<int>(std::ceil(deviceLength(spec.height))) + 1);
  const int offsetX = static_cast<int>(std::lround(
      (deviceX(spec.x) - static_cast<float>(originX)) * 256.0f));
  const int offsetY = static_cast<int>(std::lround(
      (deviceY(spec.y) - static_cast<float>(originY)) * 256.0f));
  const int radius =
      static_cast<int>(std::lround(deviceLength(spec.radius) * 64.0f));
  const Uint32 color = packColor(spec.color);
  const Uint32 inner = packColor(spec.inner);
  const int exponent = static_cast<int>(std::lround(spec.exponent * 100.0f));

  for (Layer &layer : gLayers) {
    if (layer.renderer == renderer && layer.kind == spec.kind &&
        layer.width == width && layer.height == height &&
        layer.offsetX == offsetX && layer.offsetY == offsetY &&
        layer.radius == radius && layer.strength == spec.strength &&
        layer.color == color && layer.inner == inner &&
        layer.exponent == exponent) {
      layer.used = ++gLayerClock;
      return &layer;
    }
  }

  SDL_Texture *texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                        SDL_TEXTUREACCESS_TARGET, width, height);
  if (texture == nullptr)
    return nullptr;
  if (SDL_SetRenderTarget(renderer, texture) != 0) {
    SDL_DestroyTexture(texture);
    return nullptr;
  }
  SDL_Rect clip{0, 0, 0, 0};
  const bool clipped = SDL_RenderIsClipEnabled(renderer) == SDL_TRUE;
  if (clipped)
    SDL_RenderGetClipRect(renderer, &clip);
  SDL_RenderSetClipRect(renderer, nullptr);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  SDL_RenderClear(renderer);
  gBakeShiftX = originX;
  gBakeShiftY = originY;
  paintLayer(renderer, spec);
  gBakeShiftX = 0;
  gBakeShiftY = 0;
  SDL_SetRenderTarget(renderer, nullptr);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  if (clipped)
    SDL_RenderSetClipRect(renderer, &clip);
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

  Layer layer;
  layer.texture = texture;
  layer.renderer = renderer;
  layer.kind = spec.kind;
  layer.width = width;
  layer.height = height;
  layer.offsetX = offsetX;
  layer.offsetY = offsetY;
  layer.radius = radius;
  layer.strength = spec.strength;
  layer.color = color;
  layer.inner = inner;
  layer.exponent = exponent;
  layer.used = ++gLayerClock;
  if (gLayers.size() >= kLayerLimit) {
    std::size_t oldest = 0;
    for (std::size_t i = 1; i < gLayers.size(); ++i) {
      if (gLayers[i].used < gLayers[oldest].used)
        oldest = i;
    }
    if (gLayers[oldest].texture != nullptr)
      SDL_DestroyTexture(gLayers[oldest].texture);
    gLayers.erase(gLayers.begin() + static_cast<std::ptrdiff_t>(oldest));
  }
  gLayers.push_back(layer);
  return &gLayers.back();
}

void drawLayer(SDL_Renderer *renderer, const LayerSpec &spec) {
  const int originX = static_cast<int>(std::floor(deviceX(spec.x)));
  const int originY = static_cast<int>(std::floor(deviceY(spec.y)));
  const Layer *layer = layerTexture(renderer, spec, originX, originY);
  if (layer == nullptr) {
    paintLayer(renderer, spec);
    return;
  }
  const SDL_FRect box{static_cast<float>(originX),
                      static_cast<float>(originY),
                      static_cast<float>(layer->width),
                      static_cast<float>(layer->height)};
  SDL_RenderCopyF(renderer, layer->texture, nullptr, &box);
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

// Every run of one script is drawn with one face, so that face has to own every
// code point in the run. The CJK face carries the ideographs but none of the
// arrows or the middle dot, which the Latin face has; asking the face instead
// of guessing from the byte range keeps those from turning into tofu boxes.
bool faceCovers(TTF_Font *font, const std::string &value) {
  if (font == nullptr)
    return false;
  for (std::size_t i = 0; i < value.size();) {
    const auto lead = static_cast<unsigned char>(value[i]);
    Uint32 code = lead;
    std::size_t length = 1;
    if (lead >= 0xF0U) {
      code = lead & 0x07U;
      length = 4;
    } else if (lead >= 0xE0U) {
      code = lead & 0x0FU;
      length = 3;
    } else if (lead >= 0xC0U) {
      code = lead & 0x1FU;
      length = 2;
    }
    for (std::size_t extra = 1; extra < length && i + extra < value.size(); ++extra)
      code = (code << 6) | (static_cast<unsigned char>(value[i + extra]) & 0x3FU);
    if (TTF_GlyphIsProvided32(font, code) == 0)
      return false;
    i += length;
  }
  return true;
}

TTF_Font *fontForValue(int scale, const std::string &value) {
  if (!requiresUtf8Font(value))
    return fontForScale(scale, true);
  TTF_Font *cjk = fontForScale(scale);
  if (faceCovers(cjk, value))
    return cjk;
  TTF_Font *latin = fontForScale(scale, true);
  return faceCovers(latin, value) ? latin : cjk;
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
  releaseBackdrop();
  releaseLayers();
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
  TTF_Font *font = fontForValue(scale, value);
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
    TTF_Font *font = fontForValue(scale, run);
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
  TTF_Font *font = fontForValue(scale, value);
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

int textCenteredY(const std::string &value, int centerY, int scale) {
  if (value.empty())
    return centerY;
  TTF_Font *font = fontForValue(scale, value);
  int width = 0;
  int height = 0;
  if (font == nullptr ||
      TTF_SizeUTF8(font, value.c_str(), &width, &height) != 0) {
    // The bitmap fallback draws ink straight down from y, seven pixels tall per
    // scale step.
    return centerY - 3 * scale;
  }
  TTF_Font *cjk = fontForScale(scale);
  const int baseline = cjk ? TTF_FontAscent(cjk) : TTF_FontAscent(font);
  if (gScale <= 0.0f)
    return centerY - (baseline - TTF_FontAscent(font)) - height / 2;
  // textDevice puts the rendered box at y + (baseline - ascent). Remove that
  // offset and half the box so the box centre lands on centerY.
  const float top = deviceY(static_cast<float>(centerY)) -
                    static_cast<float>(baseline - TTF_FontAscent(font)) -
                    static_cast<float>(height) / 2.0f;
  return static_cast<int>(std::lround((top - gOffsetY) / gScale));
}

std::string fitText(std::string value, int width, int scale) {
  if (width <= 0)
    return {};
  if (textWidth(value, scale) <= width)
    return value;
  const std::string suffix = "...";
  if (textWidth(suffix, scale) > width)
    return {};
  while (!value.empty() && textWidth(value + suffix, scale) > width) {
    std::size_t start = value.size() - 1;
    while (start > 0 &&
           (static_cast<unsigned char>(value[start]) & 0xc0) == 0x80)
      --start;
    value.resize(start);
  }
  return value + suffix;
}

void drawButton(SDL_Renderer *renderer, Rect bounds, float radius,
                const std::string &label, int scale, SDL_Color plate,
                SDL_Color labelColor, bool focused, float focusOpacity) {
  drawButton(renderer, bounds.x, bounds.y, bounds.w, bounds.h, radius,
             fitText(label, static_cast<int>(bounds.w - 24), scale), scale,
             plate, {0, 0, 0, 0}, labelColor, focused, focusOpacity);
}

void drawSelectionPlate(SDL_Renderer *renderer, Rect bounds, float radius,
                        SDL_Color base, SDL_Color wash, bool selected,
                        bool focused, float focusOpacity) {
  rounded(renderer, bounds.x, bounds.y, bounds.w, bounds.h, radius, base);
  if (selected) {
    if (focused)
      wash.a = static_cast<Uint8>(std::lround(wash.a *
                    std::clamp(focusOpacity, 0.0f, 1.0f)));
    rounded(renderer, bounds.x, bounds.y, bounds.w, bounds.h, radius, wash);
  }
  if (focused)
    drawFocusRing(renderer, bounds.x, bounds.y, bounds.w, bounds.h, radius,
                  focusOpacity);
}

void drawPanel(SDL_Renderer *renderer, Rect bounds, float radius,
               SDL_Color plate, Rect shadow, SDL_Color shadowColor) {
  rounded(renderer, shadow.x, shadow.y, shadow.w, shadow.h, radius, shadowColor);
  rounded(renderer, bounds.x, bounds.y, bounds.w, bounds.h, radius, plate);
}

void drawConfirmFrame(SDL_Renderer *renderer, const ConfirmLayout &layout,
                      const std::string &title, const std::string &body,
                      SDL_Color accent, Uint8 scrimAlpha) {
  fill(renderer, 0, 0, kCanvasWidth, kCanvasHeight, {2, 5, 12, scrimAlpha});
  drawPanel(renderer, layout.bounds, 28, {12, 21, 37, 245},
            layout.bounds.translated(8, 10), {0, 0, 0, 70});
  const Rect heading = layout.heading;
  rounded(renderer, heading.x, heading.y, 4, heading.h, 2, accent);
  text(renderer, heading.x + 20, heading.y + 2,
       fitText(title, static_cast<int>(heading.w - 20), 2), 2, accent);
  text(renderer, layout.body.x, layout.body.y,
       fitText(body, static_cast<int>(layout.body.w), 2), 2,
       {206, 216, 232, 255});
}

void drawChoiceButtons(SDL_Renderer *renderer, const LinearLayout &layout,
                       const std::array<std::string, 2> &labels, int choice,
                       SDL_Color accent, SDL_Color idlePlate,
                       SDL_Color idleText) {
  for (int index = 0; index < 2; ++index) {
    const bool selected = choice == index;
    drawButton(renderer, layout.item(index), 18, labels[index], 2,
               selected ? accent : idlePlate,
               selected ? SDL_Color{24, 24, 39, 255} : idleText, selected);
  }
}

void drawFocusablePlate(SDL_Renderer *renderer, float x, float y, float width,
                        float height, float radius, SDL_Color plate,
                        bool focused, float focusOpacity) {
  rounded(renderer, x, y, width, height, radius, plate);
  if (focused)
    drawFocusRing(renderer, x, y, width, height, radius, focusOpacity);
}

void drawButton(SDL_Renderer *renderer, float x, float y, float width,
                float height, float radius, const std::string &label, int scale,
                SDL_Color plate, SDL_Color rim, SDL_Color labelColor,
                bool focused, float focusOpacity) {
  if (rim.a > 0) {
    rounded(renderer, x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f,
            radius + 1.0f, rim);
  }
  drawFocusablePlate(renderer, x, y, width, height, radius, plate, focused,
                     focusOpacity);
  const int centerX = static_cast<int>(std::lround(x + width / 2.0f));
  const int centerY = static_cast<int>(std::lround(y + height / 2.0f));
  centeredText(renderer, centerX, textCenteredY(label, centerY, scale), label,
               scale, labelColor);
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
  width = deviceLength(kCanvasWidth);
  height = deviceLength(kCanvasHeight);
}

namespace {
bool sameColor(SDL_Color a, SDL_Color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Paints the whole backdrop into whatever target the renderer holds, sun
// included.
void paintBackdrop(SDL_Renderer *renderer, const Game &game, int sunX,
                   SDL_Texture *wallpaper) {
  // Every band here is walked in device rows, so the gradient is smooth at any
  // window size instead of being stretched by the renderer.
  const float left = gOffsetX;
  const float top = gOffsetY;
  const float width = deviceLength(kCanvasWidth);
  const float height = deviceLength(kCanvasHeight);
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
  circle(renderer, sunX, 138, 72,
         {game.accent.r, game.accent.g, game.accent.b, 150});
  setColor(renderer, {15, 27, 57, 175});
  std::array<SDL_FPoint, 13> far{{{deviceX(0.0f), deviceY(416.0f)},
                                 {deviceX(120.0f), deviceY(280.0f)},
                                 {deviceX(216.0f), deviceY(364.0f)},
                                 {deviceX(336.0f), deviceY(220.0f)},
                                 {deviceX(452.0f), deviceY(380.0f)},
                                 {deviceX(580.0f), deviceY(260.0f)},
                                 {deviceX(720.0f), deviceY(388.0f)},
                                 {deviceX(848.0f), deviceY(240.0f)},
                                 {deviceX(976.0f), deviceY(372.0f)},
                                 {deviceX(1124.0f), deviceY(268.0f)},
                                 {deviceX(1280.0f), deviceY(380.0f)},
                                 {deviceX(1280.0f), deviceY(720.0f)},
                                 {deviceX(0.0f), deviceY(720.0f)}}};
  SDL_RenderDrawLinesF(renderer, far.data(), 11);
  const float waveTop = deviceY(416.0f);
  for (int row = 0; row < rows; ++row) {
    const float y = top + row;
    if (y < waveTop)
      continue;
    const float logical = (y - top) / gScale;
    const float wave = 56.0f * std::sin((logical - 400.0f) * 0.01625f);
    const int shade =
        std::clamp(135 + (static_cast<int>(logical) - 416) / 2, 135, 180);
    fillDevice(renderer, left, y, width, 1.0f,
               {12, 22, 48, static_cast<Uint8>(shade)});
    if ((static_cast<int>(logical) % 27) == 0) {
      fillDevice(renderer, left, y, deviceLength(616.0f + wave), 1.0f,
                 {game.accent.r, game.accent.g, game.accent.b, 22});
    }
  }
  fill(renderer, 66, 438, 252, 32, {150, 67, 61, 120});
  fill(renderer, 66, 470, 252, 14, {85, 47, 54, 150});
  for (int i = 0; i < 3; ++i)
    fill(renderer, 158.0f + i * 42.0f, 363, 38, 38,
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

bool backdropHolds(SDL_Renderer *renderer, int width, int height,
                   const Game &game, SDL_Texture *wallpaper, int sunX) {
  return gBackdrop.texture != nullptr && gBackdrop.renderer == renderer &&
         gBackdrop.width == width && gBackdrop.height == height &&
         sameColor(gBackdrop.top, game.top) &&
         sameColor(gBackdrop.bottom, game.bottom) &&
         sameColor(gBackdrop.accent, game.accent) &&
         gBackdrop.wallpaper == wallpaper && gBackdrop.sunX == sunX;
}

// Bakes the backdrop once so a frame only has to blit it. Returns false when
// the backend cannot render into a texture, which leaves the caller painting
// the rows directly.
bool bakeBackdrop(SDL_Renderer *renderer, int width, int height,
                  const Game &game, int sunX, SDL_Texture *wallpaper) {
  if (gBackdrop.renderer != renderer)
    releaseBackdrop();
  gBackdrop.renderer = renderer;
  gBackdrop.width = width;
  gBackdrop.height = height;
  gBackdrop.top = game.top;
  gBackdrop.bottom = game.bottom;
  gBackdrop.accent = game.accent;
  gBackdrop.wallpaper = wallpaper;
  gBackdrop.sunX = sunX;
  SDL_Texture *texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                        SDL_TEXTUREACCESS_TARGET, width, height);
  if (texture == nullptr) {
    gBackdrop.unwritable = true;
    return false;
  }
  if (SDL_SetRenderTarget(renderer, texture) != 0) {
    SDL_DestroyTexture(texture);
    gBackdrop.unwritable = true;
    return false;
  }
  // Painting into the texture borrows the frame's clip and blend state, so
  // both are put back before anything is drawn on the screen.
  SDL_Rect clip{0, 0, 0, 0};
  const bool clipped = SDL_RenderIsClipEnabled(renderer) == SDL_TRUE;
  if (clipped)
    SDL_RenderGetClipRect(renderer, &clip);
  SDL_RenderSetClipRect(renderer, nullptr);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  paintBackdrop(renderer, game, sunX, wallpaper);
  SDL_SetRenderTarget(renderer, nullptr);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  if (clipped)
    SDL_RenderSetClipRect(renderer, &clip);
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
  gBackdrop.texture = texture;
  return true;
}
} // namespace

// The backdrop is a couple of thousand one-row fills and it never moves, so it
// is baked once and every frame after that is a single blit. Nothing here
// follows the clock: a still picture costs nothing to hold on screen and lets
// the loop stop drawing entirely while the shelf sits untouched.
void drawBackground(SDL_Renderer *renderer, const Game &game,
                    SDL_Texture *wallpaper) {
  if (renderer == nullptr)
    return;
  const int sunX = 1008;
  const int width = gOutputWidth;
  const int height = gOutputHeight;
  if (gBackdrop.renderer != renderer) {
    releaseBackdrop();
    gBackdrop.renderer = renderer;
  }
  if (width > 0 && height > 0 && !gBackdrop.unwritable) {
    const int focus = wallpaper != nullptr ? 0 : sunX;
    if (!backdropHolds(renderer, width, height, game, wallpaper, focus))
      bakeBackdrop(renderer, width, height, game, focus, wallpaper);
    if (gBackdrop.texture != nullptr) {
      SDL_RenderCopy(renderer, gBackdrop.texture, nullptr, nullptr);
      return;
    }
  }
  paintBackdrop(renderer, game, sunX, wallpaper);
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

std::vector<Rect> hintLayout(float rightX, float y, const std::vector<Hint> &hints) {
  std::vector<Rect> result;
  result.reserve(hints.size());
  float x = rightX - static_cast<float>(hintRowWidth(hints));
  for (const Hint &hint : hints) {
    const float width = static_cast<float>(hintWidth(hint));
    result.push_back({x, y, width, kHintChipHeight});
    x += width + kHintGap;
  }
  return result;
}

void drawHintRow(SDL_Renderer *renderer, float rightX, float y,
                 const std::vector<Hint> &hints, SDL_Color accent,
                 int focused, float focusOpacity) {
  const auto layout = hintLayout(rightX, y, hints);
  for (std::size_t index = 0; index < hints.size(); ++index) {
    const Rect bounds = layout[index];
    if (static_cast<int>(index) == focused)
      drawHintFocus(renderer, bounds.x, bounds.y, hints[index], focusOpacity);
    drawHint(renderer, bounds.x, bounds.y, hints[index], accent);
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
  // Rows land on the same whole-pixel grid rounded() draws plates on, so a ring
  // keeps the same gap above and below instead of drifting half a row low and
  // ending up flush with the control's bottom edge.
  const int firstRow = static_cast<int>(std::lround(top));
  const int rows = std::max(1, static_cast<int>(std::lround(boxHeight)));
  for (int row = 0; row < rows; ++row) {
    const float distance =
        std::min(static_cast<float>(row) + 0.5f,
                 boxHeight - static_cast<float>(row) - 0.5f);
    const float line = static_cast<float>(firstRow + row);
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

namespace {
// The badge is the same whether the row holds the keys or not: it answers
// "which key", so the eye learns one shape for keys and the ring is the only
// thing that ever moves.
constexpr SDL_Color kKeyPlate{33, 45, 68, 255};
constexpr SDL_Color kKeyInk{236, 243, 252, 255};
constexpr SDL_Color kActionRim{72, 88, 116, 255};
constexpr SDL_Color kActionInk{220, 230, 244, 255};
constexpr SDL_Color kActionPlate{24, 52, 58, 230};
constexpr SDL_Color kActionFocusInk{255, 255, 255, 255};
constexpr float kActionRadius = 8.0f;
// Three pixels of accent around a filled plate, not a ring: the frame owns the
// keys while it is focused, so a line outside it would read as a second border.
constexpr float kActionGlow = 3.0f;

// A one-character key is a circle, the way a pad button is drawn; anything
// spelled out keeps the rectangle the eye already reads as a keyboard key.
bool singleGlyph(const std::string &key) { return key.size() == 1; }

float keyBadgeWidth(const Hint &action) {
  if (singleGlyph(action.key))
    return kActionHeight;
  return std::max(kActionHeight, static_cast<float>(textWidth(action.key, 1)) +
                                     kActionPadding * 2.0f);
}

float actionFrameWidth(const Hint &action) {
  return static_cast<float>(textWidth(action.label, 1)) + kActionPadding * 2.0f;
}

float actionRowWidth(const std::vector<Hint> &actions) {
  float total = 0.0f;
  for (const Hint &action : actions)
    total += keyBadgeWidth(action) + kActionKeyGap + actionFrameWidth(action);
  if (actions.size() > 1)
    total += kActionGap * static_cast<float>(actions.size() - 1);
  return total;
}
} // namespace

int actionWidth(const Hint &action) {
  return static_cast<int>(keyBadgeWidth(action) + kActionKeyGap +
                          actionFrameWidth(action));
}

std::vector<Rect> actionLayout(float rightX, float y,
                               const std::vector<Hint> &actions) {
  std::vector<Rect> result;
  result.reserve(actions.size());
  float x = rightX - actionRowWidth(actions);
  for (const Hint &action : actions) {
    const float width =
        keyBadgeWidth(action) + kActionKeyGap + actionFrameWidth(action);
    result.push_back({x, y, width, kActionHeight});
    x += width + kActionGap;
  }
  return result;
}

void drawAction(SDL_Renderer *renderer, float x, float y, const Hint &action,
                bool focused, float focusOpacity, SDL_Color accent) {
  const float opacity = std::clamp(focusOpacity, 0.0f, 1.0f);
  const float badge = keyBadgeWidth(action);
  const float centerX = x + badge * 0.5f;
  const float centerY = y + kActionHeight * 0.5f;
  if (singleGlyph(action.key))
    circle(renderer, static_cast<int>(std::lround(centerX)),
           static_cast<int>(std::lround(centerY)),
           static_cast<int>(kActionHeight * 0.5f), kKeyPlate);
  else
    rounded(renderer, x, y, badge, kActionHeight, kActionRadius, kKeyPlate);
  centeredGlyph(renderer, static_cast<int>(std::lround(centerX)),
                static_cast<int>(std::lround(centerY)), action.key, 1, kKeyInk);

  const float frameX = x + badge + kActionKeyGap;
  const float frameWidth = actionFrameWidth(action);
  if (focused) {
    SDL_Color glow = accent;
    glow.a = static_cast<Uint8>(std::lround(60.0f * opacity));
    roundedBand(renderer, frameX - kActionGlow, y - kActionGlow,
                frameWidth + kActionGlow * 2.0f,
                kActionHeight + kActionGlow * 2.0f,
                kActionRadius + kActionGlow, kActionGlow, glow);
    SDL_Color plate = kActionPlate;
    plate.a = static_cast<Uint8>(std::lround(
        static_cast<float>(kActionPlate.a) * opacity));
    rounded(renderer, frameX, y, frameWidth, kActionHeight, kActionRadius,
            plate);
    SDL_Color rim = accent;
    rim.a = static_cast<Uint8>(std::lround(255.0f * opacity));
    roundedBand(renderer, frameX, y, frameWidth, kActionHeight, kActionRadius,
                2.0f, rim);
  } else {
    roundedBand(renderer, frameX, y, frameWidth, kActionHeight, kActionRadius,
                1.0f, kActionRim);
  }
  text(renderer, static_cast<int>(std::lround(frameX + kActionPadding)),
       textCenteredY(action.label, static_cast<int>(std::lround(centerY)), 1),
       action.label, 1, focused ? kActionFocusInk : kActionInk);
}

void drawActionRow(SDL_Renderer *renderer, float rightX, float y,
                   const std::vector<Hint> &actions, int focused,
                   float focusOpacity, SDL_Color accent) {
  const auto layout = actionLayout(rightX, y, actions);
  for (std::size_t index = 0; index < actions.size(); ++index)
    drawAction(renderer, layout[index].x, layout[index].y, actions[index],
               static_cast<int>(index) == focused, focusOpacity, accent);
}

// The shelf's card body: the gradient a card without box art shows.
void drawCardPanel(SDL_Renderer *renderer, float x, float y, float width,
                   float height, float radius, SDL_Color top, SDL_Color bottom,
                   Uint8 alpha) {
  if (renderer == nullptr)
    return;
  LayerSpec spec;
  spec.kind = LayerKind::Panel;
  spec.x = x;
  spec.y = y;
  spec.width = width;
  spec.height = height;
  spec.radius = radius;
  top.a = alpha;
  bottom.a = alpha;
  spec.color = top;
  spec.inner = bottom;
  drawLayer(renderer, spec);
}

// An empty slot: an outer plate with a slightly lighter one inside it.
void drawCardSlot(SDL_Renderer *renderer, float x, float y, float width,
                  float height, float radius, SDL_Color outer,
                  SDL_Color inner) {
  if (renderer == nullptr)
    return;
  LayerSpec spec;
  spec.kind = LayerKind::Slot;
  spec.x = x;
  spec.y = y;
  spec.width = width;
  spec.height = height;
  spec.radius = radius;
  spec.color = outer;
  spec.inner = inner;
  drawLayer(renderer, spec);
}

// The fade under a card title, so the name stays readable over box art.
void drawCardScrim(SDL_Renderer *renderer, float x, float y, float width,
                   float height, float radius, SDL_Color color, Uint8 strength,
                   float exponent) {
  if (renderer == nullptr)
    return;
  LayerSpec spec;
  spec.kind = LayerKind::Scrim;
  spec.x = x;
  spec.y = y;
  spec.width = width;
  spec.height = height;
  spec.radius = radius;
  spec.color = color;
  spec.strength = strength;
  spec.exponent = exponent;
  drawLayer(renderer, spec);
}


} // namespace launcher::ui
