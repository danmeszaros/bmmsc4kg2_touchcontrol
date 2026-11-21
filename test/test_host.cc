// Host tests: JSON lookup, value formatting, and screen rendering (written as
// PPM files to test/out/ for visual comparison with docs/mockups).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cam_state.h"
#include "format.h"
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

static void testRender(const char* outDir) {
    static uint16_t fb[gfx::W * gfx::H];
    gfx::Canvas c(fb);

    CamState s;
    s.link = s.online = true;
    s.recKnown = true;
    s.irisKnown = true; s.irisStop = 5.6f;
    s.wbKnown = s.tintKnown = true; s.wb = 5600; s.tint = 11;
    s.gainKnown = true; s.gain = 0;
    s.remainingSec = 6400;

    struct Case { const char* name; CamState st; };
    std::vector<Case> cases;
    cases.push_back({"main_standby", s});
    CamState r = s; r.recording = true; cases.push_back({"main_rec", r});
    CamState h = s; h.gain = 18; h.irisStop = 11; h.wb = 2600; h.tint = -4; cases.push_back({"main_high", h});
    CamState off; cases.push_back({"main_nocamera", off});
    CamState con; con.link = true; cases.push_back({"main_connecting", con});

    for (auto& k : cases) {
        ui::drawMain(c, k.st);
        std::string p = std::string(outDir) + "/" + k.name + ".ppm";
        writePpm(p.c_str(), fb);
    }
    // sanity: the REC ring touches the edge pixel at the top centre
    ui::drawMain(c, r);
    CHECK(fb[3 * gfx::W + gfx::CX] == ui::color::REC);
    ui::drawMain(c, s);
    CHECK(fb[3 * gfx::W + gfx::CX] == ui::color::BG);
}

int main(int argc, char** argv) {
    testJson();
    testFormat();
    testRender(argc > 1 ? argv[1] : ".");
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all host tests passed\n");
    return 0;
}
