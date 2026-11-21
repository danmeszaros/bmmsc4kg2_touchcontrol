// Screens. Layout follows docs/mockups (main screen = variant B).
// Pure drawing from CamState into a Canvas; no hardware access.
#pragma once

#include "cam_state.h"
#include "gfx.h"

namespace ui {

namespace color {
constexpr uint16_t BG = gfx::rgb(0, 0, 0);
constexpr uint16_t FG = gfx::rgb(235, 235, 235);
constexpr uint16_t DIM = gfx::rgb(120, 120, 120);
constexpr uint16_t FAINT = gfx::rgb(55, 55, 55);
constexpr uint16_t TILE = gfx::rgb(28, 28, 28);
constexpr uint16_t TILE_BORDER = gfx::rgb(70, 70, 70);
constexpr uint16_t ACC = gfx::rgb(255, 176, 0);
constexpr uint16_t REC = gfx::rgb(230, 30, 30);
constexpr uint16_t GREEN = gfx::rgb(60, 200, 90);
} // namespace color

// tap targets on the main screen (for the touch handling later)
struct Rect { int x0, y0, x1, y1; };
constexpr Rect kTileIris{34, 52, 206, 120};
constexpr Rect kTileWb{32, 126, 118, 194};
constexpr Rect kTileGain{122, 126, 208, 194};

void drawMain(gfx::Canvas& c, const CamState& s);

} // namespace ui
