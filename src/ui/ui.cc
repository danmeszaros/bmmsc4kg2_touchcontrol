#include "ui.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fonts_gen.h"
#include "format.h"

namespace ui {

using gfx::Canvas;
using gfx::CX;
using gfx::CY;
using gfx::HAlign;
using gesture::Type;
using namespace color;

const char* const kAudioInput[kAudioInputs] = {"Camera - Left", "Camera - Right", "3.5mm Left - Mic",
                                                "3.5mm Right - Mic"};
const char* const kFps[kFpsCount] = {"23.98", "24", "25", "29.97", "30", "50", "59.94", "60"};
const char* const kCodec[kCodecCount] = {"BRaw:3_1", "BRaw:5_1", "BRaw:8_1", "BRaw:12_1",
                                         "BRaw:Q0",  "BRaw:Q1",  "BRaw:Q3",  "BRaw:Q5"};
const char* const kCodecLabel[kCodecCount] = {"3:1", "5:1", "8:1", "12:1", "Q0", "Q1", "Q3", "Q5"};

static constexpr int kWbMin = 2500, kWbMax = 10000, kWbStep = 100;
static constexpr float kAudioStep = 1.0f;

// --- layout -----------------------------------------------------------------

static constexpr int kTitleY = 34;
static constexpr int kDotsY = 220;
static constexpr int kBackY = 214;
// edit screens: tap left / right of these x for -/+
static constexpr int kMinusX = 92, kPlusX = 148;
static constexpr Rect kAutoWb{CX - 50, 140, CX + 50, 192};
static constexpr Rect kBack{CX - 50, 198, CX + 50, 240};
static constexpr Rect kGainLow{30, 72, 118, 168};
static constexpr Rect kGainHigh{122, 72, 210, 168};
// audio
static constexpr int kAudioRowY[2] = {76, 128};
static constexpr int kAudioRowH0 = 22, kAudioRowH1 = 26; // above / below the row centre
static constexpr Rect kAudioPill{70, -14, 130, 6};       // relative to the row centre
static constexpr int kAudioBtnY = 184, kAudioBtnDx = 42, kAudioBtnR = 22;
// fps grid
static int fpsX(int i) { return CX - 42 + (i % 2) * 84; }
static int fpsY(int i) { return 72 + (i / 2) * 32; }
// codec grid: two columns (bitrate | quality), four rows
// BRAW: mode buttons on the left (bitrate / quality), the mode's 4 values on the right
static constexpr int kModeX = CX - 50, kModeW = 44, kModeH = 24;
static int modeY(int m) { return 97 + m * 60; }
static constexpr int kValX = CX + 46;
static int valY(int row) { return 82 + row * 30; }
static int codecModeOf(const char* codec) { return std::strncmp(codec, "BRaw:Q", 6) == 0 ? 1 : 0; }
static constexpr int kPages = 4;

// --- helpers ----------------------------------------------------------------

void audioLabel(char* out, size_t n, const char* in) {
    const char* side = std::strstr(in, "Left") ? " L" : std::strstr(in, "Right") ? " R" : std::strstr(in, "Mono") ? " M" : "";
    if (std::strncmp(in, "Camera", 6) == 0) std::snprintf(out, n, "CAM%s", side);
    else if (std::strncmp(in, "3.5mm", 5) == 0) std::snprintf(out, n, "%s%s", std::strstr(in, "Line") ? "LINE" : "JACK", side);
    else if (std::strcmp(in, "None") == 0) std::snprintf(out, n, "OFF");
    else std::snprintf(out, n, "%.6s", in);
}

float irisStep(float stop, int dir) {
    static const float s[] = {1.0f, 1.1f, 1.2f, 1.4f, 1.6f, 1.8f, 2.0f, 2.2f, 2.5f, 2.8f, 3.2f, 3.5f, 4.0f, 4.5f, 5.0f,
                              5.6f, 6.3f, 7.1f, 8.0f, 9.0f, 10.0f, 11.0f, 13.0f, 14.0f, 16.0f, 18.0f, 20.0f, 22.0f};
    const int n = sizeof s / sizeof s[0];
    if (dir > 0) {
        for (int i = 0; i < n; ++i)
            if (s[i] > stop * 1.005f) return s[i];
        return s[n - 1];
    }
    for (int i = n - 1; i >= 0; --i)
        if (s[i] < stop / 1.005f) return s[i];
    return s[0];
}

static void title(Canvas& c, const char* t) { c.text(CX, kTitleY, t, font_15b, DIM, BG); }
static void backHint(Canvas& c) { c.text(CX, kBackY, "▾ back", font_12, DIM, BG); }
static void plusMinus(Canvas& c) {
    c.text(26, CY, "−", font_34, DIM, BG);
    c.text(214, CY, "+", font_34, DIM, BG);
}

static void pageDots(Canvas& c, int page, int count) {
    const int sp = 12;
    const int x0 = CX - sp * (count - 1) / 2;
    for (int i = 0; i < count; ++i) {
        if (i == page) c.fillCircle(x0 + i * sp, kDotsY, 3, FG);
        else c.fillCircle(x0 + i * sp, kDotsY, 2, FAINT);
    }
}

static void recRing(Canvas& c, const CamState& s) {
    if (s.link && s.online && s.recording) c.ring(111, 119, REC);
}

// --- screens ----------------------------------------------------------------

static void drawMain(Canvas& c, const CamState& s) {
    char v[16], sub[16];
    const bool ok = s.link && s.online;
    if (!ok) {
        c.text(CX, 36, s.link ? "CONNECTING" : "NO CAMERA", font_13b, DIM, BG);
    } else {
        if (s.recording) {
            c.fillCircle(CX - 53, 36, 5, REC);
            c.text(CX - 43, 36, "REC", font_13b, REC, BG, HAlign::Left);
        } else {
            c.text(CX - 58, 36, "STBY", font_13b, DIM, BG, HAlign::Left);
        }
        fmt::remaining(v, sizeof v, s.remainingSec);
        c.text(CX + 58, 36, v, font_12, GREEN, BG, HAlign::Right);
    }

    const Rect& ri = kTileIris;
    c.tile(ri.x0, ri.y0, ri.x1, ri.y1, 12, 2, TILE_BORDER, TILE);
    c.text(ri.x0 + 14, 66, "IRIS", font_12b, DIM, TILE, HAlign::Left);
    if (ok && s.irisKnown) fmt::iris(v, sizeof v, s.irisStop);
    else std::snprintf(v, sizeof v, "f/--");
    c.text(CX, 94, v, font_38b, FG, TILE);

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
    pageDots(c, 0, kPages);
}

static void drawIris(Canvas& c, const CamState& s) {
    char v[16];
    title(c, "IRIS");
    if (s.irisKnown) fmt::iris(v, sizeof v, s.irisStop);
    else std::snprintf(v, sizeof v, "f/--");
    c.text(CX, 104, v, font_52b, FG, BG);
    if (s.zoomKnown) {
        std::snprintf(v, sizeof v, "%d mm", s.focalLength);
        c.text(CX, 146, v, font_14, DIM, BG);
    }
    plusMinus(c);
    // position between the lens's widest (left end) and narrowest aperture
    const float a0 = 150.0f, a1 = 390.0f;
    c.arc(104, 110, a0, a1, FAINT);
    if (s.irisKnown) {
        float f = s.irisNorm < 0 ? 0 : s.irisNorm > 1 ? 1 : s.irisNorm;
        c.arc(104, 110, a0, a0 + (a1 - a0) * f + 2.0f, ACC);
    }
    backHint(c);
}

static void drawWb(Canvas& c, const CamState& s) {
    char v[16];
    title(c, "WHITE BAL");
    if (s.wbKnown) fmt::kelvin(v, sizeof v, s.wb);
    else std::snprintf(v, sizeof v, "----K");
    c.text(CX, 96, v, font_38b, FG, BG);
    if (s.tintKnown) {
        fmt::tint(v, sizeof v, s.tint);
        c.text(CX, 128, v, font_14, DIM, BG);
    }
    plusMinus(c);
    c.tile(CX - 42, 150, CX + 42, 180, 15, 2, ACC, BG);
    c.text(CX, 165, "AUTO", font_15b, ACC, BG);
    backHint(c);
}

static void drawGain(Canvas& c, const CamState& s) {
    title(c, "BASE GAIN");
    const struct { const Rect& r; const char* label; const char* iso; int db; } box[] = {
        {kGainLow, "LOW", "ISO 400", fmt::kGainLow}, {kGainHigh, "HIGH", "ISO 3200", fmt::kGainHigh}};
    for (const auto& b : box) {
        const bool sel = s.gainKnown && s.gain == b.db;
        const int cx = (b.r.x0 + b.r.x1) / 2;
        if (sel) {
            c.fillRoundRect(b.r.x0, b.r.y0, b.r.x1, b.r.y1, 14, ACC);
            c.text(cx, 106, b.label, font_22b, BG, ACC);
            c.text(cx, 138, b.iso, font_12, BG, ACC);
        } else {
            c.tile(b.r.x0, b.r.y0, b.r.x1, b.r.y1, 14, 2, FAINT, BG);
            c.text(cx, 106, b.label, font_22b, FG, BG);
            c.text(cx, 138, b.iso, font_12, DIM, BG);
        }
    }
    if (s.gainKnown && s.gain != fmt::kGainLow && s.gain != fmt::kGainHigh) {
        char v[24];
        std::snprintf(v, sizeof v, "now %+d dB", s.gain);
        c.text(CX, 188, v, font_12, ACC, BG);
    } else {
        c.text(CX, 188, "0 dB / +18 dB", font_10, DIM, BG);
    }
    backHint(c);
}

static void drawAudio(Canvas& c, const CamState& s, int sel) {
    title(c, "AUDIO");
    char lab[16], lvl[16];
    for (int ch = 0; ch < 2; ++ch) {
        const AudioChannel& a = s.audio[ch];
        const int y = kAudioRowY[ch];
        if (ch == sel) c.tile(26, y - kAudioRowH0, 214, y + kAudioRowH1, 10, 2, ACC, BG);
        std::snprintf(lab, sizeof lab, "CH%d", ch + 1);
        c.text(36, y - 6, lab, font_13b, DIM, BG, HAlign::Left);
        const Rect p{kAudioPill.x0, y + kAudioPill.y0, kAudioPill.x1, y + kAudioPill.y1};
        c.fillRoundRect(p.x0, p.y0, p.x1, p.y1, 10, PILL);
        if (a.inputKnown) audioLabel(lab, sizeof lab, a.input);
        else std::snprintf(lab, sizeof lab, "--");
        c.text((p.x0 + p.x1) / 2, y - 4, lab, font_12b, FG, PILL);
        if (a.gainKnown) std::snprintf(lvl, sizeof lvl, "%+.0f dB", (double)a.gain);
        else std::snprintf(lvl, sizeof lvl, "-- dB");
        c.text(204, y - 5, lvl, font_16b, FG, BG, HAlign::Right);
        c.fillRect(40, y + 12, 200, y + 16, FAINT);
        if (a.gainKnown && a.rangeKnown && a.gainMax > a.gainMin) {
            float f = (a.gain - a.gainMin) / (a.gainMax - a.gainMin);
            f = f < 0 ? 0 : f > 1 ? 1 : f;
            c.fillRect(40, y + 12, 40 + (int)(160 * f), y + 16, ch == sel ? ACC : DIM);
        }
    }
    for (int i = 0; i < 2; ++i) {
        const int x = CX + (i ? kAudioBtnDx : -kAudioBtnDx);
        c.fillCircle(x, kAudioBtnY, kAudioBtnR, TILE_BORDER);
        c.fillCircle(x, kAudioBtnY, kAudioBtnR - 2, TILE);
        c.text(x, kAudioBtnY - 2, i ? "+" : "−", font_34, FG, TILE);
    }
    pageDots(c, 1, kPages);
}

// one choice of a two-tap grid: current = filled, pending = outlined accent;
// locked (recording) = all grey, the current one still filled
static void choice(Canvas& c, int x, int y, const char* label, bool cur, bool pending, bool locked) {
    if (cur) {
        const uint16_t bg = locked ? FAINT : ACC;
        c.fillRoundRect(x - 38, y - 13, x + 38, y + 13, 12, bg);
        c.text(x, y, label, font_16b, locked ? DIM : BG, bg);
    } else if (locked) {
        c.tile(x - 38, y - 13, x + 38, y + 13, 12, 1, FAINT, BG);
        c.text(x, y, label, font_16b, FAINT, BG);
    } else {
        c.tile(x - 38, y - 13, x + 38, y + 13, 12, pending ? 3 : 2, pending ? ACC : FAINT, BG);
        c.text(x, y, label, font_16b, pending ? ACC : FG, BG);
    }
}

static void lockedHint(Canvas& c) { c.text(CX, 200, "locked while recording", font_12, REC, BG); }

static void drawFps(Canvas& c, const CamState& s, int pending) {
    title(c, "FRAME RATE");
    const bool locked = s.recording;
    for (int i = 0; i < kFpsCount; ++i)
        choice(c, fpsX(i), fpsY(i), kFps[i], std::strcmp(s.fps, kFps[i]) == 0, i == pending, locked);
    if (locked) {
        lockedHint(c);
    } else if (pending >= 0) {
        char v[32];
        std::snprintf(v, sizeof v, "tap again to set %s", kFps[pending]);
        c.text(CX, 200, v, font_12, ACC, BG);
    } else {
        c.text(CX, 200, "shutter stays 180°", font_10, DIM, BG);
    }
    pageDots(c, 3, kPages);
}

static void drawQuality(Canvas& c, const CamState& s, int mode, int pending) {
    title(c, "BRAW");
    bool known = false;
    for (int i = 0; i < kCodecCount; ++i) known |= std::strcmp(s.codec, kCodec[i]) == 0;
    const int curMode = known ? codecModeOf(s.codec) : -1;
    const char* curLabel = "";
    for (int i = 0; i < kCodecCount; ++i)
        if (std::strcmp(s.codec, kCodec[i]) == 0) curLabel = kCodecLabel[i];
    static const char* const kModeLabel[2] = {"BITRATE", "QUALITY"};
    const bool locked = s.recording;
    for (int m = 0; m < 2; ++m) {
        // listed mode: amber outline; under the camera's current mode its value in amber
        const int y = modeY(m);
        const bool shown = m == mode;
        const uint16_t edge = locked ? FAINT : shown ? ACC : FAINT;
        c.tile(kModeX - kModeW, y - kModeH, kModeX + kModeW, y + kModeH, 14, shown && !locked ? 3 : locked ? 1 : 2, edge, BG);
        c.text(kModeX, y - 6, kModeLabel[m], font_13b, locked ? (shown ? DIM : FAINT) : shown ? FG : DIM, BG);
        if (m == curMode) c.text(kModeX, y + 11, curLabel, font_12b, locked ? DIM : ACC, BG);
        else c.text(kModeX, y + 11, "constant", font_10, locked ? FAINT : DIM, BG);
    }
    for (int row = 0; row < 4; ++row) {
        const int i = mode * 4 + row;
        choice(c, kValX, valY(row), kCodecLabel[i], std::strcmp(s.codec, kCodec[i]) == 0, i == pending, locked);
    }
    char v[32];
    if (locked) {
        lockedHint(c);
    } else if (pending >= 0) {
        std::snprintf(v, sizeof v, "tap again to set %s", kCodecLabel[pending]);
        c.text(CX, 200, v, font_12, ACC, BG);
    } else if (s.codec[0] && !known) {
        std::snprintf(v, sizeof v, "now %s", s.codec); // something not in the grid
        c.text(CX, 200, v, font_12, ACC, BG);
    }
    pageDots(c, 2, kPages);
}

void Ui::draw(Canvas& c, const CamState& s, uint32_t nowMs) const {
    c.clear(BG);
    recRing(c, s);
    const bool ok = s.link && s.online;
    switch (ok ? screen_ : Screen::Main) {
    case Screen::Main: drawMain(c, s); break;
    case Screen::Iris: drawIris(c, s); break;
    case Screen::Wb: drawWb(c, s); break;
    case Screen::Gain: drawGain(c, s); break;
    case Screen::Audio: drawAudio(c, s, audioSel_); break;
    case Screen::Fps:
        drawFps(c, s, (int32_t)(nowMs - pendingUntil_) < 0 ? pending_ : -1);
        break;
    case Screen::Quality:
        drawQuality(c, s, codecMode_ >= 0 ? codecMode_ : codecModeOf(s.codec),
                    (int32_t)(nowMs - pendingUntil_) < 0 ? pending_ : -1);
        break;
    }
    // diagnostic line (button presses and their results) over the bottom of the screen
    if (s.note[0] && (int32_t)(nowMs - s.noteUntilMs) < 0) {
        c.fillRoundRect(CX - 78, kBackY - 11, CX + 78, kBackY + 11, 10, TILE);
        c.text(CX, kBackY, s.note, font_13b, FG, TILE);
    }
}

// --- input ------------------------------------------------------------------

bool Ui::tick(uint32_t nowMs) {
    bool redraw = false;
    if (pending_ >= 0 && (int32_t)(nowMs - pendingUntil_) >= 0) {
        pending_ = -1;
        redraw = true;
    }
    if (screen_ != Screen::Main && nowMs - lastInput_ >= kIdleBackMs) {
        screen_ = Screen::Main;
        redraw = true;
    }
    return redraw;
}

bool Ui::onGesture(const gesture::Event& ev, const CamState& s, CameraCtl& cam, uint32_t nowMs) {
    if (ev.type == Type::None) return false;
    lastInput_ = nowMs;
    if (!(s.link && s.online)) {
        screen_ = Screen::Main;
        return false;
    }
    const bool page = screen_ == Screen::Main || screen_ == Screen::Audio || screen_ == Screen::Fps ||
                      screen_ == Screen::Quality;
    switch (ev.type) {
    case Type::SwipeLeft: // next page
        if (screen_ == Screen::Main) screen_ = Screen::Audio;
        else if (screen_ == Screen::Audio) screen_ = Screen::Quality;
        else if (screen_ == Screen::Quality) screen_ = Screen::Fps;
        else return false;
        pending_ = -1;
        codecMode_ = -1; // BRAW page opens on the camera's current mode
        return true;
    case Type::SwipeRight:
        if (screen_ == Screen::Fps) screen_ = Screen::Quality;
        else if (screen_ == Screen::Quality) screen_ = Screen::Audio;
        else if (screen_ == Screen::Audio) screen_ = Screen::Main;
        else return false;
        pending_ = -1;
        codecMode_ = -1;
        return true;
    case Type::SwipeDown:
    case Type::SwipeUp: // either vertical swipe leaves an edit screen
        if (page) return false;
        screen_ = Screen::Main;
        return true;
    case Type::Tap:
        return onTap(ev.x, ev.y, false, s, cam, nowMs);
    case Type::Repeat:
        return onTap(ev.x, ev.y, true, s, cam, nowMs);
    default:
        return false;
    }
}

bool Ui::onTap(int x, int y, bool repeat, const CamState& s, CameraCtl& cam, uint32_t nowMs) {
    const int dir = x < kMinusX ? -1 : x >= kPlusX ? 1 : 0;
    // edit screens: the "back" hint at the bottom is a button too
    if (!repeat && kBack.contains(x, y) &&
        (screen_ == Screen::Iris || screen_ == Screen::Wb || screen_ == Screen::Gain)) {
        screen_ = Screen::Main;
        return true;
    }
    switch (screen_) {
    case Screen::Main:
        if (repeat) return false;
        if (kTileIris.contains(x, y)) screen_ = Screen::Iris;
        else if (kTileWb.contains(x, y)) screen_ = Screen::Wb;
        else if (kTileGain.contains(x, y)) screen_ = Screen::Gain;
        else return false;
        return true;

    case Screen::Iris:
        if (!dir || !s.irisKnown) return false;
        cam.setIris(irisStep(s.irisStop, dir));
        return true;

    case Screen::Wb:
        if (kAutoWb.contains(x, y)) {
            if (!repeat) cam.autoWb();
            return false;
        }
        if (!dir || !s.wbKnown) return false;
        {
            // step to the next multiple of 100 K
            int k = dir > 0 ? (s.wb / kWbStep + 1) * kWbStep : ((s.wb + kWbStep - 1) / kWbStep - 1) * kWbStep;
            k = k < kWbMin ? kWbMin : k > kWbMax ? kWbMax : k;
            if (k != s.wb) cam.setWb(k);
        }
        return true;

    case Screen::Gain:
        if (repeat) return false;
        if (x < CX) cam.setGain(fmt::kGainLow);
        else cam.setGain(fmt::kGainHigh);
        return true;

    case Screen::Audio: {
        // +/- buttons
        for (int i = 0; i < 2; ++i) {
            const int bx = CX + (i ? kAudioBtnDx : -kAudioBtnDx);
            const int dx = x - bx, dy = y - kAudioBtnY;
            if (dx * dx + dy * dy <= (kAudioBtnR + 8) * (kAudioBtnR + 8)) {
                const AudioChannel& a = s.audio[audioSel_];
                if (!a.gainKnown || !a.rangeKnown) return false;
                float g = std::round(a.gain) + (i ? kAudioStep : -kAudioStep);
                g = g < a.gainMin ? a.gainMin : g > a.gainMax ? a.gainMax : g;
                if (g != a.gain) cam.setAudioGain(audioSel_, g);
                return true;
            }
        }
        if (repeat) return false;
        for (int ch = 0; ch < 2; ++ch) {
            const int ry = kAudioRowY[ch];
            if (y < ry - kAudioRowH0 || y >= ry + kAudioRowH1) continue;
            // a tap on the other channel only moves the focus there
            if (ch != audioSel_) {
                audioSel_ = ch;
                return true;
            }
            // on the focused channel the source pill (with some margin) cycles the input
            if (x >= kAudioPill.x0 - 10 && x < kAudioPill.x1 + 10) {
                const AudioChannel& a = s.audio[ch];
                int next = 0;
                for (int i = 0; i < kAudioInputs; ++i)
                    if (a.inputKnown && std::strcmp(a.input, kAudioInput[i]) == 0) next = (i + 1) % kAudioInputs;
                cam.setAudioInput(ch, kAudioInput[next]);
            }
            return true;
        }
        return false;
    }

    case Screen::Fps:
        if (repeat) return false;
        if (s.recording) { // greyed out mid-take; the camera layer refuses it anyway
            pending_ = -1;
            return false;
        }
        for (int i = 0; i < kFpsCount; ++i) {
            if (std::abs(x - fpsX(i)) <= 40 && std::abs(y - fpsY(i)) <= 15) {
                if (std::strcmp(s.fps, kFps[i]) == 0) {
                    pending_ = -1;
                } else if (pending_ == i && (int32_t)(nowMs - pendingUntil_) < 0) {
                    cam.setFps(kFps[i]); // second tap: apply
                    pending_ = -1;
                } else {
                    pending_ = i;
                    pendingUntil_ = nowMs + kConfirmMs;
                }
                return true;
            }
        }
        return false;

    case Screen::Quality: {
        if (repeat) return false;
        if (s.recording) { // greyed out mid-take, also the list switch
            pending_ = -1;
            return false;
        }
        const int mode = codecMode_ >= 0 ? codecMode_ : codecModeOf(s.codec);
        // left: switch which values are listed (nothing is sent)
        for (int m = 0; m < 2; ++m) {
            if (std::abs(x - kModeX) <= kModeW + 4 && std::abs(y - modeY(m)) <= kModeH + 4) {
                if (m == mode) return false;
                codecMode_ = m;
                pending_ = -1;
                return true;
            }
        }
        // right: the values of the listed mode
        for (int row = 0; row < 4; ++row) {
            const int i = mode * 4 + row;
            if (std::abs(x - kValX) <= 40 && std::abs(y - valY(row)) <= 14) {
                if (std::strcmp(s.codec, kCodec[i]) == 0) {
                    pending_ = -1;
                } else if (pending_ == i && (int32_t)(nowMs - pendingUntil_) < 0) {
                    cam.setCodec(kCodec[i]); // second tap: apply
                    pending_ = -1;
                } else {
                    pending_ = i;
                    pendingUntil_ = nowMs + kConfirmMs;
                }
                return true;
            }
        }
        return false;
    }
    }
    return false;
}

} // namespace ui
