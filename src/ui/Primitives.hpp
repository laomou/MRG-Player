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

[[nodiscard]] int hintWidth(const Hint &hint);
[[nodiscard]] int hintRowWidth(const std::vector<Hint> &hints);
void drawHint(SDL_Renderer *renderer, float x, float y, const Hint &hint,
              SDL_Color accent);
// Lays a legend row out so its last chip ends at rightX, keeping footers off
// the top edge where they would compete with the screen title.
void drawHintRow(SDL_Renderer *renderer, float rightX, float y,
                 const std::vector<Hint> &hints, SDL_Color accent);

} // namespace launcher::ui
