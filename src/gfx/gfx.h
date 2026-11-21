// Minimal drawing into a 240x240 RGB565 framebuffer. Hardware independent
// (host tests render it to PNG, see test/).
#pragma once

#include <cstddef>
#include <cstdint>

#include "font.h"

namespace gfx {

constexpr int W = 240;
constexpr int H = 240;
constexpr int CX = W / 2;
constexpr int CY = H / 2;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

enum class HAlign : uint8_t { Left, Center, Right };

class Canvas {
public:
    explicit Canvas(uint16_t* fb) : fb_(fb) {}
    uint16_t* data() { return fb_; }

    void clear(uint16_t c);
    void fillRect(int x0, int y0, int x1, int y1, uint16_t c); // [x0,x1) x [y0,y1)
    void fillRoundRect(int x0, int y0, int x1, int y1, int r, uint16_t c);
    // outlined tile: border of width bw in `border`, inside filled with `fill`
    void tile(int x0, int y0, int x1, int y1, int r, int bw, uint16_t border, uint16_t fill);
    void fillCircle(int cx, int cy, int r, uint16_t c);
    // ring centred on the screen between radii rIn..rOut
    void ring(int rIn, int rOut, uint16_t c);
    // part of a centred ring from angle a0 to a1 (degrees, 0 = 3 o'clock,
    // clockwise, a1 > a0, may exceed 360)
    void arc(int rIn, int rOut, float a0, float a1, uint16_t c);

    // text: x by `ha`, y = vertical middle of the font's ascent+descent (like
    // PIL anchor "m"); `bg` is the colour behind the text (for antialiasing)
    void text(int x, int y, const char* s, const Font& f, uint16_t fg, uint16_t bg, HAlign ha = HAlign::Center);
    static int textWidth(const char* s, const Font& f);

private:
    void blend(int x, int y, uint16_t fg, uint16_t bg, uint8_t a); // a = 0..15
    void hline(int x0, int x1, int y, uint16_t c);
    uint16_t* fb_;
};

// decodes one UTF-8 code point, advances s; returns 0 at the end
uint32_t nextCodepoint(const char*& s);
const Glyph* findGlyph(const Font& f, uint32_t cp);

} // namespace gfx
