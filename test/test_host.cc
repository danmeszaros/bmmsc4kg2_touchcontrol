// Host tests: JSON lookup, value formatting, and screen rendering (written as
// PPM files to test/out/ for visual comparison with docs/mockups).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cam_state.h"
#include "format.h"
#include "gesture.h"
#include "gfx.h"
#include "json.h"
#include "ui.h"

static int failures = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

static bool streq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

static void testJson() {
    double d;
    bool b;
    std::string s;
    // bodies as returned by the camera (docs/camera-quirks.md in the explorer)
    const std::string iris = "{\"apertureNumber\": 380, \"apertureStop\": 5.5, "
                             "\"continuousApertureAutoExposure\": false, \"normalised\": 0.017}";
    CHECK(json::number(iris, "apertureStop", d) && d == 5.5);
    CHECK(json::number(iris, "apertureNumber", d) && d == 380);
    CHECK(json::boolean(iris, "continuousApertureAutoExposure", b) && !b);
    CHECK(!json::number(iris, "missing", d));

    CHECK(json::number("{\n    \"gain\": 18\n}", "gain", d) && d == 18);
    CHECK(json::number("{\"whiteBalanceTint\": -12}", "whiteBalanceTint", d) && d == -12);
    CHECK(json::boolean("{\"recording\":true}", "recording", b) && b);
    CHECK(json::bareBool("true", b) && b);
    CHECK(json::bareBool(" false\n", b) && !b);

    const std::string ws = "{\"size\": 1, \"workingset\": [{\"activeDisk\": true, \"clipCount\": 5, "
                           "\"deviceName\": \"usb16640\", \"index\": 0, \"remainingRecordTime\": 6670, "
                           "\"remainingSpace\": 256064094208, \"totalSpace\": 256637403136, \"volume\": \"A002\"}]}";
    CHECK(json::number(ws, "remainingRecordTime", d) && d == 6670);
    CHECK(json::string(ws, "volume", s) && s == "A002");

    const std::string fmtBody = "{\"codec\": \"BRaw:8_1\", \"frameRate\": \"25\", \"offSpeedEnabled\": false}";
    CHECK(json::string(fmtBody, "frameRate", s) && s == "25");
    CHECK(json::string(fmtBody, "codec", s) && s == "BRaw:8_1");
    CHECK(json::string("{\"mode\": \"Off\", \"type\": \"\"}", "type", s) && s.empty());
    // a key that also appears as a string value
    CHECK(json::string("{\"a\": \"mode\", \"mode\": \"Off\"}", "mode", s) && s == "Off");
}

static void testFormat() {
    char b[32];
    fmt::iris(b, sizeof b, 5.6f); CHECK(streq(b, "f/5.6"));
    fmt::iris(b, sizeof b, 5.5f); CHECK(streq(b, "f/5.5"));
    fmt::iris(b, sizeof b, 8.0f); CHECK(streq(b, "f/8"));
    fmt::iris(b, sizeof b, 6.325f); CHECK(streq(b, "f/6.3"));
    fmt::iris(b, sizeof b, 11.0f); CHECK(streq(b, "f/11"));
    fmt::iris(b, sizeof b, 9.1f); CHECK(streq(b, "f/9.1"));
    fmt::kelvin(b, sizeof b, 5600); CHECK(streq(b, "5600K"));
    fmt::tint(b, sizeof b, 11); CHECK(streq(b, "tint +11"));
    fmt::tint(b, sizeof b, -3); CHECK(streq(b, "tint -3"));
    fmt::tint(b, sizeof b, 0); CHECK(streq(b, "tint +0"));
    fmt::remaining(b, sizeof b, 6670); CHECK(streq(b, "1:51 left"));
    fmt::remaining(b, sizeof b, 59); CHECK(streq(b, "0:00 left"));
    fmt::remaining(b, sizeof b, -1); CHECK(streq(b, "--:-- left"));
    fmt::gainLabel(b, sizeof b, 0); CHECK(streq(b, "LOW"));
    fmt::gainLabel(b, sizeof b, 18); CHECK(streq(b, "HIGH"));
    fmt::gainLabel(b, sizeof b, 6); CHECK(streq(b, "+6 dB"));
    fmt::gainLabel(b, sizeof b, -12); CHECK(streq(b, "-12 dB"));
    // measured on the camera: ISO = 400 * 2^(dB/6), nominal third stops
    const struct { int db; const char* iso; } t[] = {{-12, "ISO 100"}, {-10, "ISO 125"}, {0, "ISO 400"}, {2, "ISO 500"},
                                                      {10, "ISO 1250"}, {18, "ISO 3200"}, {28, "ISO 10000"}, {36, "ISO 25600"}};
    for (auto& e : t) { fmt::isoForGain(b, sizeof b, e.db); CHECK(streq(b, e.iso)); }
}

static void writePpm(const char* path, const uint16_t* fb) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::printf("cannot write %s\n", path); ++failures; return; }
    std::fprintf(f, "P6\n%d %d\n255\n", gfx::W, gfx::H);
    for (int i = 0; i < gfx::W * gfx::H; ++i) {
        uint16_t p = fb[i];
        uint8_t rgb[3] = {(uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

struct FakeCam : CameraCtl {
    std::vector<std::string> calls;
    void setIris(float f) override { char b[16]; std::snprintf(b, sizeof b, "iris %.2f", (double)f); calls.push_back(b); }
    void setWb(int k) override { calls.push_back("wb " + std::to_string(k)); }
    void autoWb() override { calls.push_back("autowb"); }
    void setGain(int d) override { calls.push_back("gain " + std::to_string(d)); }
    void setAudioInput(int ch, const char* in) override { calls.push_back("ain " + std::to_string(ch) + " " + in); }
    void setAudioGain(int ch, float d) override { calls.push_back("alvl " + std::to_string(ch) + " " + std::to_string((int)d)); }
    void setFps(const char* f) override { calls.push_back(std::string("fps ") + f); }
};

static CamState sampleState() {
    CamState s;
    s.link = s.online = true;
    s.recKnown = true;
    s.irisKnown = true; s.irisStop = 5.6f; s.irisNorm = 0.03f;
    s.zoomKnown = true; s.focalLength = 34;
    s.wbKnown = s.tintKnown = true; s.wb = 5600; s.tint = 11;
    s.gainKnown = true; s.gain = 0;
    s.remainingSec = 6400;
    std::snprintf(s.fps, sizeof s.fps, "25");
    std::snprintf(s.audio[0].input, sizeof s.audio[0].input, "3.5mm Left - Mic");
    s.audio[0].inputKnown = s.audio[0].gainKnown = s.audio[0].rangeKnown = true;
    s.audio[0].gain = 20; s.audio[0].gainMin = -10; s.audio[0].gainMax = 40;
    std::snprintf(s.audio[1].input, sizeof s.audio[1].input, "Camera - Right");
    s.audio[1].inputKnown = s.audio[1].gainKnown = s.audio[1].rangeKnown = true;
    s.audio[1].gain = 0; s.audio[1].gainMin = -6; s.audio[1].gainMax = 6;
    return s;
}

static void testGesture() {
    using gesture::Type;
    gesture::Recognizer r;
    uint32_t t = 1000;
    auto feed = [&](bool d, int x, int y, uint32_t dt) { t += dt; return r.update(d, x, y, t).type; };
    CHECK(feed(true, 100, 100, 0) == Type::None);
    CHECK(feed(true, 102, 101, 15) == Type::None);
    CHECK(feed(false, 0, 0, 15) == Type::Tap);
    feed(true, 180, 120, 100); feed(true, 120, 122, 15);
    CHECK(feed(false, 0, 0, 15) == Type::SwipeLeft);
    feed(true, 60, 120, 100); feed(true, 130, 118, 15);
    CHECK(feed(false, 0, 0, 15) == Type::SwipeRight);
    feed(true, 120, 60, 100); feed(true, 118, 140, 15);
    CHECK(feed(false, 0, 0, 15) == Type::SwipeDown);
    feed(true, 200, 120, 100);
    int repeats = 0;
    for (int i = 0; i < 60; ++i) if (feed(true, 201, 121, 15) == Type::Repeat) ++repeats;
    CHECK(repeats >= 3 && repeats <= 5);
    CHECK(feed(false, 0, 0, 15) == Type::None);
    feed(true, 100, 100, 100); feed(true, 125, 100, 15);
    CHECK(feed(false, 0, 0, 15) == Type::None);
}

static void testHelpers() {
    char b[16];
    CHECK(ui::irisStep(5.6f, 1) == 6.3f);
    CHECK(ui::irisStep(5.6f, -1) == 5.0f);
    CHECK(ui::irisStep(5.5f, 1) == 5.6f);
    CHECK(ui::irisStep(6.325f, 1) == 7.1f);
    CHECK(ui::irisStep(22.0f, 1) == 22.0f);
    ui::audioLabel(b, sizeof b, "3.5mm Left - Mic"); CHECK(streq(b, "JACK L"));
    ui::audioLabel(b, sizeof b, "3.5mm Right - Line"); CHECK(streq(b, "LINE R"));
    ui::audioLabel(b, sizeof b, "Camera - Mono"); CHECK(streq(b, "CAM M"));
    ui::audioLabel(b, sizeof b, "None"); CHECK(streq(b, "OFF"));
}

static void testUiFlow() {
    using gesture::Event;
    using gesture::Type;
    ui::Ui u;
    FakeCam cam;
    CamState s = sampleState();
    uint32_t t = 0;
    auto ev = [&](Type ty, int x = 0, int y = 0) { Event e; e.type = ty; e.x = x; e.y = y; t += 200; return u.onGesture(e, s, cam, t); };

    CHECK(ev(Type::Tap, 120, 90) && u.screen() == ui::Screen::Iris);
    ev(Type::Tap, 200, 120);
    ev(Type::Tap, 30, 120);
    CHECK(cam.calls.size() == 2 && cam.calls[0] == "iris 6.30" && cam.calls[1] == "iris 5.00");
    CHECK(ev(Type::SwipeDown) && u.screen() == ui::Screen::Main);

    cam.calls.clear();
    ev(Type::Tap, 70, 160); CHECK(u.screen() == ui::Screen::Wb);
    ev(Type::Tap, 220, 110); ev(Type::Tap, 20, 110); ev(Type::Tap, 120, 165);
    CHECK(cam.calls.size() == 3 && cam.calls[0] == "wb 5700" && cam.calls[1] == "wb 5500" && cam.calls[2] == "autowb");
    s.wb = 10000; cam.calls.clear(); ev(Type::Tap, 220, 110); CHECK(cam.calls.empty());
    s.wb = 2650; ev(Type::Tap, 20, 110); CHECK(cam.calls.size() == 1 && cam.calls[0] == "wb 2600");
    ev(Type::SwipeDown);

    cam.calls.clear();
    ev(Type::Tap, 165, 160); CHECK(u.screen() == ui::Screen::Gain);
    ev(Type::Tap, 170, 120); ev(Type::Tap, 70, 120);
    CHECK(cam.calls.size() == 2 && cam.calls[0] == "gain 18" && cam.calls[1] == "gain 0");
    ev(Type::SwipeDown);

    CHECK(ev(Type::SwipeLeft) && u.screen() == ui::Screen::Audio);
    cam.calls.clear();
    ev(Type::Tap, 110, 72);
    ev(Type::Tap, 120 + 42, 184);
    ev(Type::Tap, 190, 128);
    ev(Type::Tap, 120 - 42, 184);
    s.audio[1].gain = -6; ev(Type::Tap, 78, 184);
    CHECK(cam.calls.size() == 3);
    CHECK(cam.calls.size() > 0 && cam.calls[0] == "ain 0 3.5mm Right - Mic");
    CHECK(cam.calls.size() > 1 && cam.calls[1] == "alvl 0 21");
    CHECK(cam.calls.size() > 2 && cam.calls[2] == "alvl 1 -1");

    CHECK(ev(Type::SwipeLeft) && u.screen() == ui::Screen::Fps);
    cam.calls.clear();
    ev(Type::Tap, 120 + 42, 72 + 2 * 32); // "50": pending
    CHECK(cam.calls.empty());
    ev(Type::Tap, 120 + 42, 72 + 2 * 32); // second tap: apply
    CHECK(cam.calls.size() == 1 && cam.calls[0] == "fps 50");
    CHECK(!ev(Type::SwipeLeft));
    CHECK(ev(Type::SwipeRight) && u.screen() == ui::Screen::Audio);

    t += ui::Ui::kIdleBackMs + 1;
    CHECK(u.tick(t) && u.screen() == ui::Screen::Main);
}

static void testRender(const char* outDir) {
    static uint16_t fb[gfx::W * gfx::H];
    gfx::Canvas c(fb);
    ui::Ui u;
    const CamState s = sampleState();

    struct Case { const char* name; ui::Screen sc; CamState st; };
    std::vector<Case> cases;
    cases.push_back({"main_standby", ui::Screen::Main, s});
    CamState r = s; r.recording = true; cases.push_back({"main_rec", ui::Screen::Main, r});
    CamState h = s; h.gain = 18; h.irisStop = 11; h.wb = 2600; h.tint = -4; cases.push_back({"main_high", ui::Screen::Main, h});
    CamState off; cases.push_back({"main_nocamera", ui::Screen::Main, off});
    CamState con; con.link = true; cases.push_back({"main_connecting", ui::Screen::Main, con});
    cases.push_back({"edit_iris", ui::Screen::Iris, s});
    CamState i2 = s; i2.irisStop = 11; i2.irisNorm = 0.5f; cases.push_back({"edit_iris_f11", ui::Screen::Iris, i2});
    cases.push_back({"edit_wb", ui::Screen::Wb, s});
    cases.push_back({"edit_gain", ui::Screen::Gain, s});
    CamState g6 = s; g6.gain = 6; cases.push_back({"edit_gain_other", ui::Screen::Gain, g6});
    cases.push_back({"page_audio", ui::Screen::Audio, s});
    cases.push_back({"page_fps", ui::Screen::Fps, s});
    cases.push_back({"page_fps_rec", ui::Screen::Fps, r});

    for (auto& k : cases) {
        u.show(k.sc, 0);
        u.draw(c, k.st, 0);
        std::string p = std::string(outDir) + "/" + k.name + ".ppm";
        writePpm(p.c_str(), fb);
    }
    u.show(ui::Screen::Main, 0);
    u.draw(c, r, 0);
    CHECK(fb[3 * gfx::W + gfx::CX] == ui::color::REC);
    u.draw(c, s, 0);
    CHECK(fb[3 * gfx::W + gfx::CX] == ui::color::BG);
}

int main(int argc, char** argv) {
    testJson();
    testFormat();
    testGesture();
    testHelpers();
    testUiFlow();
    testRender(argc > 1 ? argv[1] : ".");
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all host tests passed\n");
    return 0;
}
