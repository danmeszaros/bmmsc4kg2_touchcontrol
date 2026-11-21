// Bitmap font format produced by tools/mkfont.py: 4 bits per pixel
// (0 = background, 15 = full ink), rows packed two pixels per byte, high
// nibble first, each row padded to a whole byte.
#pragma once

#include <cstdint>

struct Glyph {
    uint32_t cp;      // unicode code point
    uint32_t offset;  // into Font::bits
    uint8_t w, h;     // bitmap size
    int8_t x, y;      // bitmap top-left relative to the pen on the baseline
    uint8_t adv;      // pen advance
};

struct Font {
    uint8_t ascent, descent; // pixels above / below the baseline
    uint16_t count;
    const Glyph* glyphs;     // sorted by cp
    const uint8_t* bits;
};
