/** @file font.h
 *  @brief Tiny built-in 5x7 bitmap font (ASCII 32..126) drawn with filled
 *  rectangles, so the GUI needs nothing beyond SDL2 itself. */
#pragma once
#include <SDL.h>
#include <string>

namespace gui
{

constexpr int kGlyphW   = 5; /**< glyph width in font pixels */
constexpr int kGlyphH   = 7; /**< glyph height in font pixels */
constexpr int kGlyphAdv = 6; /**< horizontal advance (one pixel of spacing) */

/** Width in device pixels of `text` when each font pixel is `px` device pixels
 *  wide (the trailing spacing column is not counted). */
int TextWidth(const std::string& text, int px);

/** Height in device pixels of one line of text. */
inline int TextHeight(int px)
{
    return kGlyphH * px;
}

/** Draws `text` with its top-left corner at (x, y) device pixels. Each set
 *  font pixel becomes a px x px filled rectangle. Characters outside
 *  32..126 are drawn as '?'. */
void DrawText(SDL_Renderer* r, int x, int y, int px, SDL_Color color, const std::string& text);

} // namespace gui
