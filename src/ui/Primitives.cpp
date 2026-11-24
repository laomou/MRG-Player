#include "ui/Primitives.hpp"

#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>

namespace launcher::ui {
namespace {
constexpr float kPi = 3.14159265358979323846f;
using Glyph = std::array<std::uint8_t, 7>;
std::array<TTF_Font *, 6> gFonts{};
std::string gFontPath;
std::array<TTF_Font *, 6> gLatinFonts{};
std::string gLatinFontPath;

bool requiresUtf8Font(const std::string &value) {
  return std::any_of(value.begin(), value.end(),
                     [](unsigned char ch) { return ch >= 0x80; });
}

TTF_Font *fontForScale(int scale, bool latin = false) {
  scale = std::clamp(scale, 1, 5);
  TTF_Font *&font =
      (latin ? gLatinFonts : gFonts)[static_cast<std::size_t>(scale)];
  const auto &path = latin ? gLatinFontPath : gFontPath;
  constexpr std::array<int, 6> sizes{0, 14, 20, 28, 36, 44};
  if (font == nullptr && !path.empty()) {
    font = TTF_OpenFont(path.c_str(), sizes[scale]);
    if (font != nullptr)
      TTF_SetFontHinting(font, TTF_HINTING_NORMAL);
  }
  return font;
}

void setColor(SDL_Renderer *renderer, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
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
  gLatinFontPath =
      (std::filesystem::path(fontPath).parent_path() / "DejaVuSans.ttf")
          .string();
  return fontForScale(2) != nullptr;
}

void shutdownTextRenderer() {
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
  gLatinFontPath.clear();
  gFontPath.clear();
  if (TTF_WasInit() != 0)
    TTF_Quit();
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
  const SDL_FRect rect{x, y, width, height};
  SDL_RenderFillRectF(renderer, &rect);
}

void circle(SDL_Renderer *renderer, int centerX, int centerY, int radius,
            SDL_Color color) {
  setColor(renderer, color);
  for (int dy = -radius; dy <= radius; ++dy) {
    const int dx = static_cast<int>(
        std::sqrt(static_cast<float>(radius * radius - dy * dy)));
    SDL_RenderDrawLine(renderer, centerX - dx, centerY + dy, centerX + dx,
                       centerY + dy);
  }
}

void rounded(SDL_Renderer *renderer, float x, float y, float width,
             float height, float radius, SDL_Color color) {
  if (width <= 0 || height <= 0)
    return;
  radius = std::clamp(radius, 0.0f, std::min(width, height) * 0.5f);
  // A single span per row avoids double-compositing translucent surfaces.
  for (int row = 0; row < static_cast<int>(height); ++row) {
    const float distance = std::min(row + 0.5f, height - row - 0.5f);
    const float yy = std::max(0.0f, radius - distance);
    const float inset =
        radius - std::sqrt(std::max(0.0f, radius * radius - yy * yy));
    fill(renderer, x + inset, y + row, width - 2 * inset, 1, color);
  }
}

int bitmapTextAdvance(const std::string &value, int scale) {
  return static_cast<int>(value.size()) * 6 * scale;
}

void drawBitmapText(SDL_Renderer *renderer, int x, int y,
                    const std::string &value, int scale, SDL_Color color) {
  setColor(renderer, color);
  int cursor = x;
  for (char raw : value) {
    const char ch =
        (raw >= 'a' && raw <= 'z') ? static_cast<char>(raw - 'a' + 'A') : raw;
    const Glyph bits = glyph(ch);
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if ((bits[row] & (1u << (4 - col))) != 0u) {
          const SDL_Rect pixel{cursor + col * scale, y + row * scale, scale,
                               scale};
          SDL_RenderFillRect(renderer, &pixel);
        }
      }
    }
    cursor += 6 * scale;
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
  return width;
}

void text(SDL_Renderer *renderer, int x, int y, const std::string &value,
          int scale, SDL_Color color) {
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
    SDL_Surface *surface =
        font != nullptr ? TTF_RenderUTF8_Blended(font, run.c_str(), color)
                        : nullptr;
    if (surface != nullptr) {
      SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
      if (texture != nullptr) {
        // Align mixed Latin/CJK runs on a shared baseline.
        TTF_Font *cjk = fontForScale(scale);
        const int baseline = cjk ? TTF_FontAscent(cjk) : TTF_FontAscent(font);
        const SDL_Rect destination{cursor, y + baseline - TTF_FontAscent(font),
                                   surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
        SDL_DestroyTexture(texture);
      }
      cursor += surface->w;
      SDL_FreeSurface(surface);
    }
    begin = end;
  }
}

void centeredText(SDL_Renderer *renderer, int centerX, int y,
                  const std::string &value, int scale, SDL_Color color) {
  text(renderer, centerX - textWidth(value, scale) / 2, y, value, scale, color);
}

void drawBackground(SDL_Renderer *renderer, const Game &game, float time) {
  for (int y = 0; y < kLogicalHeight; ++y) {
    const float t = static_cast<float>(y) / kLogicalHeight;
    setColor(renderer, mix(game.top, game.bottom, t));
    SDL_RenderDrawLine(renderer, 0, y, kLogicalWidth, y);
  }
  const int sunX = 1260 + static_cast<int>(std::sin(time * 0.08f) * 22.0f);
  circle(renderer, sunX, 172, 90,
         {game.accent.r, game.accent.g, game.accent.b, 150});
  setColor(renderer, {15, 27, 57, 175});
  std::array<SDL_Point, 13> far{{{0, 520},
                                 {150, 350},
                                 {270, 455},
                                 {420, 275},
                                 {565, 475},
                                 {725, 325},
                                 {900, 485},
                                 {1060, 300},
                                 {1220, 465},
                                 {1405, 335},
                                 {1600, 475},
                                 {1600, 900},
                                 {0, 900}}};
  SDL_RenderDrawLines(renderer, far.data(), 11);
  for (int y = 520; y < 900; ++y) {
    const float wave = 70.0f * std::sin((y - 500) * 0.013f);
    fill(renderer, 0, static_cast<float>(y), kLogicalWidth, 1,
         {12, 22, 48, static_cast<Uint8>(135 + std::min(90, y - 520) / 2)});
    if ((y % 34) == 0)
      fill(renderer, 0, static_cast<float>(y), 770 + wave, 2,
           {game.accent.r, game.accent.g, game.accent.b, 22});
  }
  fill(renderer, 82, 548, 315, 40, {150, 67, 61, 120});
  fill(renderer, 82, 588, 315, 17, {85, 47, 54, 150});
  fill(renderer, 1110, 510, 390, 40, {150, 67, 61, 110});
  for (int i = 0; i < 3; ++i)
    fill(renderer, 198.0f + i * 52.0f, 454, 48, 48,
         {game.accent.r, game.accent.g, game.accent.b, 135});
  for (int y = 0; y < kLogicalHeight; ++y) {
    const float t = static_cast<float>(y) / kLogicalHeight;
    const Uint8 alpha = static_cast<Uint8>(18 + 185 * t * t);
    fill(renderer, 0, static_cast<float>(y), kLogicalWidth, 1,
         {4, 8, 20, alpha});
  }
  for (int y = 0; y < kLogicalHeight; y += 4)
    fill(renderer, 0, static_cast<float>(y), kLogicalWidth, 2,
         {255, 255, 255, 4});
}

namespace {
// Footer legend metrics. The chip is deliberately the same height as a scale 1
// line of text plus padding so a labelled hint sits on the footer baseline.
constexpr float kHintChipHeight = 26.0f;
constexpr float kHintChipPadding = 9.0f;
constexpr float kHintMinChipWidth = 26.0f;
constexpr float kHintLabelGap = 10.0f;
constexpr float kHintGap = 34.0f;

[[nodiscard]] float chipWidth(const Hint &hint) {
  return std::max(kHintMinChipWidth,
                  static_cast<float>(textWidth(hint.key, 1)) +
                      kHintChipPadding * 2.0f);
}
} // namespace

int hintWidth(const Hint &hint) {
  return textWidth(hint.label, 1) + static_cast<int>(kHintLabelGap) +
         static_cast<int>(chipWidth(hint));
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
  text(renderer, static_cast<int>(x), static_cast<int>(y) + 6, hint.label, 1,
       {143, 163, 181, 255});
  const float chipX =
      x + static_cast<float>(textWidth(hint.label, 1)) + kHintLabelGap;
  const float width = chipWidth(hint);
  rounded(renderer, chipX, y, width, kHintChipHeight, 8,
          hint.accent ? accent : SDL_Color{255, 255, 255, 26});
  centeredText(renderer, static_cast<int>(chipX + width * 0.5f),
               static_cast<int>(y) + 6, hint.key, 1,
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

} // namespace launcher::ui
