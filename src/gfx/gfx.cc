#include "gfx.h"

#include <cmath>

namespace gfx {

void Canvas::clear(uint16_t c) {
    for (int i = 0; i < W * H; ++i) fb_[i] = c;
}

void Canvas::hline(int x0, int x1, int y, uint16_t c) {
    if (y < 0 || y >= H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > W) x1 = W;
    uint16_t* p = fb_ + y * W;
    for (int x = x0; x < x1; ++x) p[x] = c;
}

void Canvas::fillRect(int x0, int y0, int x1, int y1, uint16_t c) {
    for (int y = y0; y < y1; ++y) hline(x0, x1, y, c);
}

void Canvas::fillRoundRect(int x0, int y0, int x1, int y1, int r, uint16_t c) {
    const int h = y1 - y0, w = x1 - x0;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (int y = y0; y < y1; ++y) {
        int dy = 0;
        if (y < y0 + r) dy = y0 + r - y;
        else if (y >= y1 - r) dy = y - (y1 - r - 1);
        int inset = 0;
        if (dy > 0) {
            float d = (float)dy - 0.5f;
            inset = r - (int)std::lround(std::sqrt(std::fmax(0.0f, (float)(r * r) - d * d)));
        }
        hline(x0 + inset, x1 - inset, y, c);
    }
}

void Canvas::tile(int x0, int y0, int x1, int y1, int r, int bw, uint16_t border, uint16_t fill) {
    fillRoundRect(x0, y0, x1, y1, r, border);
    fillRoundRect(x0 + bw, y0 + bw, x1 - bw, y1 - bw, r > bw ? r - bw : 0, fill);
}

void Canvas::fillCircle(int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; ++dy) {
        int dx = (int)std::lround(std::sqrt((float)(r * r - dy * dy)));
        hline(cx - dx, cx + dx + 1, cy + dy, c);
    }
}

void Canvas::ring(int rIn, int rOut, uint16_t c) {
    const float ro2 = (float)rOut * rOut, ri2 = (float)rIn * rIn;
    for (int y = 0; y < H; ++y) {
        float dy = (float)y + 0.5f - CY;
        float dy2 = dy * dy;
        if (dy2 > ro2) continue;
        int xo = (int)std::lround(std::sqrt(ro2 - dy2));
        if (dy2 >= ri2) {
            hline(CX - xo, CX + xo, y, c);
        } else {
            int xi = (int)std::lround(std::sqrt(ri2 - dy2));
            hline(CX - xo, CX - xi, y, c);
            hline(CX + xi, CX + xo, y, c);
        }
    }
}

void Canvas::arc(int rIn, int rOut, float a0, float a1, uint16_t c) {
    if (a1 <= a0) return;
    const float ro2 = (float)rOut * rOut, ri2 = (float)rIn * rIn;
    for (int y = CY - rOut; y <= CY + rOut; ++y) {
        if (y < 0 || y >= H) continue;
        float dy = (float)y + 0.5f - CY;
        for (int x = CX - rOut; x <= CX + rOut; ++x) {
            float dx = (float)x + 0.5f - CX;
            float d2 = dx * dx + dy * dy;
            if (d2 > ro2 || d2 < ri2) continue;
            float a = std::atan2(dy, dx) * 57.29578f; // -180..180, clockwise (y down)
            if (a < a0) a += 360.0f;
            if (a < a0) a += 360.0f;
            if (a <= a1 && x >= 0 && x < W) fb_[y * W + x] = c;
        }
    }
}

static inline uint16_t mix565(uint16_t fg, uint16_t bg, uint8_t a) {
    if (a >= 15) return fg;
    if (a == 0) return bg;
    int r = ((fg >> 11) * a + (bg >> 11) * (15 - a) + 7) / 15;
    int g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (15 - a) + 7) / 15;
    int b = ((fg & 31) * a + (bg & 31) * (15 - a) + 7) / 15;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void Canvas::blend(int x, int y, uint16_t fg, uint16_t bg, uint8_t a) {
    if (x < 0 || y < 0 || x >= W || y >= H || a == 0) return;
    fb_[y * W + x] = mix565(fg, bg, a);
}

uint32_t nextCodepoint(const char*& s) {
    const uint8_t* p = (const uint8_t*)s;
    uint32_t c = *p;
    if (!c) return 0;
    int n = 0;
    if (c < 0x80) n = 0;
    else if ((c & 0xE0) == 0xC0) { c &= 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; n = 3; }
    else { s += 1; return '?'; }
    ++p;
    for (int i = 0; i < n; ++i) {
        if ((*p & 0xC0) != 0x80) { s = (const char*)p; return '?'; }
        c = (c << 6) | (*p++ & 0x3F);
    }
    s = (const char*)p;
    return c;
}

const Glyph* findGlyph(const Font& f, uint32_t cp) {
    int lo = 0, hi = f.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f.glyphs[mid].cp == cp) return &f.glyphs[mid];
        if (f.glyphs[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return cp == '?' ? nullptr : findGlyph(f, '?');
}

int Canvas::textWidth(const char* s, const Font& f) {
    int w = 0;
    while (uint32_t cp = nextCodepoint(s)) {
        if (const Glyph* g = findGlyph(f, cp)) w += g->adv;
    }
    return w;
}

void Canvas::text(int x, int y, const char* s, const Font& f, uint16_t fg, uint16_t bg, HAlign ha) {
    int pen = x;
    if (ha == HAlign::Center) pen -= textWidth(s, f) / 2;
    else if (ha == HAlign::Right) pen -= textWidth(s, f);
    const int baseline = y + (f.ascent - f.descent) / 2;
    while (uint32_t cp = nextCodepoint(s)) {
        const Glyph* g = findGlyph(f, cp);
        if (!g) continue;
        const uint8_t* bits = f.bits + g->offset;
        const int stride = (g->w + 1) / 2;
        for (int row = 0; row < g->h; ++row) {
            const uint8_t* r = bits + row * stride;
            for (int col = 0; col < g->w; ++col) {
                uint8_t a = (col & 1) ? (r[col / 2] & 0x0F) : (r[col / 2] >> 4);
                blend(pen + g->x + col, baseline + g->y + row, fg, bg, a);
            }
        }
        pen += g->adv;
    }
}

} // namespace gfx
