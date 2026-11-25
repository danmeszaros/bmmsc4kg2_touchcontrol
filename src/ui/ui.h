// Screens and touch handling. Layout follows docs/ui.md and docs/mockups.
// Pure: draws CamState into a Canvas and turns gestures into CameraCtl calls,
// no hardware access (host tests render every screen to PNG).
#pragma once

#include "cam_state.h"
#include "gesture.h"
#include "gfx.h"

namespace ui {

namespace color {
constexpr uint16_t BG = gfx::rgb(0, 0, 0);
constexpr uint16_t FG = gfx::rgb(235, 235, 235);
constexpr uint16_t DIM = gfx::rgb(120, 120, 120);
constexpr uint16_t FAINT = gfx::rgb(55, 55, 55);
constexpr uint16_t TILE = gfx::rgb(28, 28, 28);
constexpr uint16_t TILE_BORDER = gfx::rgb(70, 70, 70);
constexpr uint16_t PILL = gfx::rgb(40, 40, 40);
constexpr uint16_t ACC = gfx::rgb(255, 176, 0);
constexpr uint16_t REC = gfx::rgb(230, 30, 30);
constexpr uint16_t GREEN = gfx::rgb(60, 200, 90);
} // namespace color

struct Rect {
    int x0, y0, x1, y1;
    bool contains(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

// main screen tiles
constexpr Rect kTileIris{34, 52, 206, 120};
constexpr Rect kTileWb{32, 126, 118, 194};
constexpr Rect kTileGain{122, 126, 208, 194};

enum class Screen : uint8_t { Main, Audio, Fps, Quality, Iris, Wb, Gain };

// audio input cycle for one channel (CAM L -> CAM R -> JACK L -> JACK R)
constexpr int kAudioInputs = 4;
extern const char* const kAudioInput[kAudioInputs];
// short label for any input name the camera reports ("JACK L", "CAM M", ...)
void audioLabel(char* out, size_t n, const char* input);

constexpr int kFpsCount = 8;
extern const char* const kFps[kFpsCount];

// BRAW codecs: [0..3] constant bitrate (left column), [4..7] constant quality
constexpr int kCodecCount = 8;
extern const char* const kCodec[kCodecCount];      // API value, "BRaw:8_1"
extern const char* const kCodecLabel[kCodecCount]; // "8:1"

// third-stop f-number series; next/previous from the current value
float irisStep(float stop, int dir);

class Ui {
public:
    static constexpr uint32_t kIdleBackMs = 15000; // back to MAIN without input
    static constexpr uint32_t kConfirmMs = 3000; // FPS / BRAW: second tap within this

    void draw(gfx::Canvas& c, const CamState& s, uint32_t nowMs) const;
    // true if the screen needs a redraw
    bool onGesture(const gesture::Event& ev, const CamState& s, CameraCtl& cam, uint32_t nowMs);
    bool tick(uint32_t nowMs);

    Screen screen() const { return screen_; }
    void show(Screen s, uint32_t nowMs) { screen_ = s; lastInput_ = nowMs; codecMode_ = -1; }

private:
    bool onTap(int x, int y, bool repeat, const CamState& s, CameraCtl& cam, uint32_t nowMs);

    Screen screen_ = Screen::Main;
    int audioSel_ = 0;
    // FPS / BRAW page: item marked by the first tap
    int pending_ = -1;
    uint32_t pendingUntil_ = 0;
    int codecMode_ = -1; // BRAW page: 0 bitrate, 1 quality, -1 = the camera's current mode
    uint32_t lastInput_ = 0;
};

} // namespace ui
