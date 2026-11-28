#pragma once

#include "model/Game.hpp"
#include "ui/Canvas.hpp"
#include "ui/Layout.hpp"

#include <SDL.h>

#include <string>
#include <vector>

namespace launcher::ui {

bool initializeTextRenderer(const std::string &fontPath);
void shutdownTextRenderer();

// The interface is laid out on the canvas in Canvas.hpp but drawn straight into
// the window at the size the window really has: text is
// rasterised at device pixels instead of being upscaled from the canvas, which
// is where the jagged widget text came from. A window that is not 16:9 gets the
// canvas letterboxed, and every draw call still speaks canvas units.
void setOutputSize(int outputWidth, int outputHeight);
// The window size in the units SDL reports mouse positions in. A HiDPI display
// hands out points that are smaller than the framebuffer it renders into, so the
// pointer has to be scaled up by that ratio before it meets the canvas.
void setWindowSize(int windowWidth, int windowHeight);
// Maps a window coordinate, such as a mouse event, onto the canvas. Window
// points are converted to output pixels first, so a click lands on the widget
// it was aimed at whatever the display scaling is.
void windowToCanvas(float windowX, float windowY, float &canvasX,
                    float &canvasY);
// Where the canvas landed in output pixels, so the console picture can be
// placed inside the same letterbox as the widgets.
void canvasRect(float &x, float &y, float &width, float &height);

// Clip rectangles are given in canvas units and converted to output pixels, so
// a scrolled row clips at the same place whatever the window size is.
void setClipRect(SDL_Renderer *renderer, float x, float y, float width,
                 float height);
void clearClipRect(SDL_Renderer *renderer);

SDL_Color mix(SDL_Color a, SDL_Color b, float t);
void fill(SDL_Renderer *renderer, float x, float y, float width, float height,
          SDL_Color color);
void circle(SDL_Renderer *renderer, int centerX, int centerY, int radius,
            SDL_Color color);
void rounded(SDL_Renderer *renderer, float x, float y, float width,
             float height, float radius, SDL_Color color);
// Vertical gradient clipped to a rounded rectangle. The rows are walked in
// output pixels, so a card body fades evenly instead of banding when the canvas
// is scaled up.
void gradientRounded(SDL_Renderer *renderer, float x, float y, float width,
                     float height, float radius, SDL_Color top,
                     SDL_Color bottom);
// The same fade, but only the bottom corners are rounded: used for the shade
// the card titles sit on.
void fadeBottomRounded(SDL_Renderer *renderer, float x, float y, float width,
                       float height, float radius, SDL_Color color,
                       Uint8 strength, float exponent);
// The shelf redraws the same cards every frame, so each of their layers is
// baked once into a texture and copied afterwards. The rasterised result is
// identical: the layers keep the per-pixel alpha the direct draw produced.
void drawCardPanel(SDL_Renderer *renderer, float x, float y, float width,
                   float height, float radius, SDL_Color top, SDL_Color bottom,
                   Uint8 alpha);
void drawCardSlot(SDL_Renderer *renderer, float x, float y, float width,
                  float height, float radius, SDL_Color outer,
                  SDL_Color inner);
void drawCardScrim(SDL_Renderer *renderer, float x, float y, float width,
                   float height, float radius, SDL_Color color, Uint8 strength,
                   float exponent);

// Draws a texture into a canvas-space rectangle; the game picture, box art and
// save thumbnails all go through here so they follow the same transform.
void drawTexture(SDL_Renderer *renderer, SDL_Texture *texture, float x, float y,
                 float width, float height);
int textWidth(const std::string &value, int scale);
// Truncates at UTF-8 codepoint boundaries; even the ellipsis must fit.
std::string fitText(std::string value, int width, int scale);
void text(SDL_Renderer *renderer, int x, int y, const std::string &value,
          int scale, SDL_Color color);
void centeredText(SDL_Renderer *renderer, int centerX, int y,
                  const std::string &value, int scale, SDL_Color color);
// Centres a short label - a button glyph, a platform badge - inside a circle or
// a pill. text() sits runs on the CJK baseline, so this uses the rendered box
// instead of a hand-tuned offset.
void centeredGlyph(SDL_Renderer *renderer, int centerX, int centerY,
                   const std::string &value, int scale, SDL_Color color);
// The y origin to hand to text() so the rendered box of value lands on
// centerY. TTF ink starts a few pixels below the requested origin and mixed
// runs share the CJK baseline, which is why labels used to carry hand-tuned
// offsets (+10, +7, +4): this measures the box instead.
[[nodiscard]] int textCenteredY(const std::string &value, int centerY,
                                int scale);

// A reusable control surface for menu rows and buttons. Keeping the plate and
// focus state in one call prevents a screen from filling a selected row but
// forgetting the system focus ring. Callers may draw their icon and text after
// this; the ring is outside the plate, so their content cannot cover it.
void drawFocusablePlate(SDL_Renderer *renderer, float x, float y, float width,
                        float height, float radius, SDL_Color plate,
                        bool focused, float focusOpacity = 1.0f);

// A button: plate, optional hairline rim, a label centred with textCenteredY,
// and the shared focus ring when focused. Buttons go through this so the label
// is centred by construction and focus looks the same everywhere.
void drawButton(SDL_Renderer *renderer, float x, float y, float width,
                float height, float radius, const std::string &label, int scale,
                SDL_Color plate, SDL_Color rim, SDL_Color labelColor,
                bool focused, float focusOpacity = 1.0f);
// Rect-based controls use the exact geometry handed to hit testing.
void drawButton(SDL_Renderer *renderer, Rect bounds, float radius,
                const std::string &label, int scale, SDL_Color plate,
                SDL_Color labelColor, bool focused, float focusOpacity = 1.0f);
void drawSelectionPlate(SDL_Renderer *renderer, Rect bounds, float radius,
                        SDL_Color base, SDL_Color wash, bool selected,
                        bool focused, float focusOpacity = 1.0f);
void drawPanel(SDL_Renderer *renderer, Rect bounds, float radius,
               SDL_Color plate, Rect shadow, SDL_Color shadowColor);
// Simple confirmations share a frame and two buttons, not their business action.
void drawConfirmFrame(SDL_Renderer *renderer, const ConfirmLayout &layout,
                      const std::string &title, const std::string &body,
                      SDL_Color accent, Uint8 scrimAlpha);
void drawChoiceButtons(SDL_Renderer *renderer, const LinearLayout &layout,
                       const std::array<std::string, 2> &labels, int choice,
                       SDL_Color accent,
                       SDL_Color idlePlate = {255, 255, 255, 18},
                       SDL_Color idleText = {230, 235, 245, 255});
// wallpaper, when present, replaces the drawn skyline with a soft version of
// the selected game's cover, dimmed so the shelf stays readable.
void drawBackground(SDL_Renderer *renderer, const Game &game,
                    SDL_Texture *wallpaper = nullptr, bool scene = true);

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
std::vector<Rect> hintLayout(float rightX, float y, const std::vector<Hint> &hints);
void drawHintRow(SDL_Renderer *renderer, float rightX, float y,
                 const std::vector<Hint> &hints, SDL_Color accent,
                 int focused = -1, float focusOpacity = 1.0f);

// The action row is the same pair of ideas written as buttons: the key sits in
// a badge that never changes colour, because it answers "which key" and not
// "where are the keys", and the action is written in a frame that takes the
// plate and the ring. A legend can only be read; this row can be walked to, so
// it is laid out and hit-tested through the same numbers.
inline constexpr float kActionHeight = 30.0f;
inline constexpr float kActionKeyGap = 10.0f;
inline constexpr float kActionPadding = 11.0f;
inline constexpr float kActionGap = 22.0f;

[[nodiscard]] int actionWidth(const Hint &action);
// Lays the row out so its last frame ends at rightX, the way hintLayout does
// for a legend.
std::vector<Rect> actionLayout(float rightX, float y,
                               const std::vector<Hint> &actions);
// One entry of the strip: a filled key badge and the word it acts on. The word
// carries no plate and no outline, because the strip is read at a glance and a
// frame around every word would draw a second row of boxes under the content.
void drawAction(SDL_Renderer *renderer, float x, float y, const Hint &action);
void drawActionRow(SDL_Renderer *renderer, float rightX, float y,
                   const std::vector<Hint> &actions);

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
