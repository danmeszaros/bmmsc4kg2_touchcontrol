#include "ui.h"

#include <cstdio>

#include "fonts_gen.h"
#include "format.h"

namespace ui {

using gfx::Canvas;
using gfx::CX;
using gfx::HAlign;
using namespace color;

static void statusLine(Canvas& c, const CamState& s) {
    char buf[24];
    if (!s.link || !s.online) {
        c.text(CX, 36, s.link ? "CONNECTING" : "NO CAMERA", font_13b, DIM, BG);
        return;
    }
    if (s.recording) {
        c.ring(111, 119, REC);
        c.fillCircle(CX - 53, 36, 5, REC);
        c.text(CX - 43, 36, "REC", font_13b, REC, BG, HAlign::Left);
    } else {
        c.text(CX - 58, 36, "STBY", font_13b, DIM, BG, HAlign::Left);
    }
    fmt::remaining(buf, sizeof buf, s.remainingSec);
    c.text(CX + 58, 36, buf, font_12, GREEN, BG, HAlign::Right);
}

static void pageDots(Canvas& c, int page, int count) {
    const int sp = 12;
    const int x0 = CX - sp * (count - 1) / 2;
    for (int i = 0; i < count; ++i) {
        if (i == page) c.fillCircle(x0 + i * sp, 220, 3, FG);
        else c.fillCircle(x0 + i * sp, 220, 2, FAINT);
    }
}

void drawMain(Canvas& c, const CamState& s) {
    char v[16], sub[16];
    c.clear(BG);
    statusLine(c, s);
    const bool ok = s.link && s.online;

    // IRIS
    const Rect& ri = kTileIris;
    c.tile(ri.x0, ri.y0, ri.x1, ri.y1, 12, 2, TILE_BORDER, TILE);
    c.text(ri.x0 + 14, 66, "IRIS", font_12b, DIM, TILE, HAlign::Left);
    if (ok && s.irisKnown) fmt::iris(v, sizeof v, s.irisStop);
    else std::snprintf(v, sizeof v, "f/--");
    c.text(CX, 94, v, font_38b, FG, TILE);

    // WB
    const Rect& rw = kTileWb;
    const int cw = (rw.x0 + rw.x1) / 2;
    c.tile(rw.x0, rw.y0, rw.x1, rw.y1, 12, 2, TILE_BORDER, TILE);
    c.text(cw, 140, "WB", font_11b, DIM, TILE);
    if (ok && s.wbKnown) fmt::kelvin(v, sizeof v, s.wb);
    else std::snprintf(v, sizeof v, "----K");
    c.text(cw, 164, v, font_20b, FG, TILE);
    if (ok && s.tintKnown) {
        fmt::tint(sub, sizeof sub, s.tint);
        c.text(cw, 184, sub, font_10, DIM, TILE);
    }

    // GAIN
    const Rect& rg = kTileGain;
    const int cg = (rg.x0 + rg.x1) / 2;
    c.tile(rg.x0, rg.y0, rg.x1, rg.y1, 12, 2, TILE_BORDER, TILE);
    c.text(cg, 140, "GAIN", font_11b, DIM, TILE);
    if (ok && s.gainKnown) {
        fmt::gainLabel(v, sizeof v, s.gain);
        fmt::isoForGain(sub, sizeof sub, s.gain);
        c.text(cg, 164, v, font_20b, FG, TILE);
        c.text(cg, 184, sub, font_10, DIM, TILE);
    } else {
        c.text(cg, 164, "--", font_20b, FG, TILE);
    }

    pageDots(c, 0, 3);
}

} // namespace ui
