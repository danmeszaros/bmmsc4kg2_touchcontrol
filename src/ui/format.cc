#include "format.h"

#include <cmath>
#include <cstdio>

namespace fmt {

void iris(char* out, size_t n, float stop) {
    float r = std::round(stop * 10.0f) / 10.0f;
    if (r >= 10.0f || std::fabs(r - std::round(r)) < 0.05f) std::snprintf(out, n, "f/%d", (int)std::lround(r));
    else std::snprintf(out, n, "f/%.1f", (double)r);
}

void kelvin(char* out, size_t n, int k) { std::snprintf(out, n, "%dK", k); }

void tint(char* out, size_t n, int t) { std::snprintf(out, n, "tint %+d", t); }

void remaining(char* out, size_t n, int32_t sec) {
    if (sec < 0) { std::snprintf(out, n, "--:-- left"); return; }
    int32_t min = sec / 60;
    std::snprintf(out, n, "%d:%02d left", (int)(min / 60), (int)(min % 60));
}

void gainLabel(char* out, size_t n, int db) {
    if (db == kGainLow) std::snprintf(out, n, "LOW");
    else if (db == kGainHigh) std::snprintf(out, n, "HIGH");
    else std::snprintf(out, n, "%+d dB", db);
}

void isoForGain(char* out, size_t n, int db) {
    long iso = std::lround(400.0 * std::pow(2.0, db / 6.0));
    // the camera shows the nominal third-stop ISO series
    static const long series[] = {100, 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1250, 1600, 2000,
                                  2500, 3200, 4000, 5000, 6400, 8000, 10000, 12800, 16000, 20000, 25600};
    long best = iso;
    double bestErr = 1e9;
    for (long s : series) {
        double e = std::fabs(std::log((double)s / (double)iso));
        if (e < bestErr) { bestErr = e; best = s; }
    }
    if (bestErr > 0.05) best = iso;
    std::snprintf(out, n, "ISO %ld", best);
}

} // namespace fmt
