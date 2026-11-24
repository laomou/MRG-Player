#pragma once

#include "model/Game.hpp"

#include <SDL.h>

#include <string>
#include <vector>

namespace launcher::ui {

inline constexpr int kLogicalWidth = 1600;
inline constexpr int kLogicalHeight = 900;

bool initializeTextRenderer(const std::string &fontPath);
void shutdownTextRenderer();

SDL_Color mix(SDL_Color a, SDL_Color b, float t);
void fill(SDL_Renderer *renderer, float x, float y, float width, float height,
          SDL_Color color);
void circle(SDL_Renderer *renderer, int centerX, int centerY, int radius,
            SDL_Color color);
void rounded(SDL_Renderer *renderer, float x, float y, float width,
             float height, float radius, SDL_Color color);
int textWidth(const std::string &value, int scale);
void text(SDL_Renderer *renderer, int x, int y, const std::string &value,
          int scale, SDL_Color color);
void centeredText(SDL_Renderer *renderer, int centerX, int y,
                  const std::string &value, int scale, SDL_Color color);
// Centres a short label - a button glyph, a platform badge - inside a circle or
// a pill. text() sits runs on the CJK baseline, so this uses the rendered box
// instead of a hand-tuned offset.
void centeredGlyph(SDL_Renderer *renderer, int centerX, int centerY,
                   const std::string &value, int scale, SDL_Color color);
void drawBackground(SDL_Renderer *renderer, const Game &game, float time);

// One footer legend entry: what the control does, then the control itself, the
// way a console writes "Back  B". Reading left to right gives the action first
// and the button second, so the footer stays a legend rather than a key list.
struct Hint {
  std::string label;
  std::string key;
  // The accent draws the chip filled, for the action that moves the screen on.
  bool accent = false;
};

// Shared with callers that hit-test a legend row, so a click lands on the same
// box the renderer drew.
inline constexpr float kHintChipHeight = 26.0f;
inline constexpr float kHintGap = 34.0f;

[[nodiscard]] int hintWidth(const Hint &hint);
[[nodiscard]] int hintRowWidth(const std::vector<Hint> &hints);
void drawHint(SDL_Renderer *renderer, float x, float y, const Hint &hint,
              SDL_Color accent);
// Lays a legend row out so its last chip ends at rightX, keeping footers off
// the top edge where they would compete with the screen title.
void drawHintRow(SDL_Renderer *renderer, float rightX, float y,
                 const std::vector<Hint> &hints, SDL_Color accent);
// The focus ring never takes the accent of the game or the section on
// screen: it answers "where do the keys go", which is a property of the
// system, so it stays one colour everywhere.
inline constexpr SDL_Color kFocusColor{215, 226, 242, 255};

// Draws the ring that shows which legend entry the direction keys are on.
// opacity fades the ring in when the focus lands on a new control, and 0
// skips it entirely.
void drawHintFocus(SDL_Renderer *renderer, float x, float y, const Hint &hint,
                   float opacity);
// The same ring for any other control: two pixels, three pixels outside the
// rectangle, no halo. It is drawn last so it never disturbs the control or
// the background underneath it.
void drawFocusRing(SDL_Renderer *renderer, float x, float y, float width,
                   float height, float radius, float opacity);

// A gear, drawn from geometry rather than a glyph: neither bundled font ships
// a symbol for "settings", and the question mark it replaces read as help.
// The silhouette is rasterised once and tinted per call.
void gearIcon(SDL_Renderer *renderer, float centerX, float centerY,
              float radius, SDL_Color color);

} // namespace launcher::ui
