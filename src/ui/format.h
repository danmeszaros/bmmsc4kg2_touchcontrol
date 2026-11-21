// Text for displayed values. Pure functions, unit-tested on the host.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fmt {

constexpr int kGainLow = 0;   // dual native gain (Blackmagic spec): 0 dB / +18 dB
constexpr int kGainHigh = 18;

void iris(char* out, size_t n, float stop);           // "f/5.6", "f/11"
void kelvin(char* out, size_t n, int k);               // "5600K"
void tint(char* out, size_t n, int t);                 // "tint +11"
void remaining(char* out, size_t n, int32_t sec);      // "1:46 left", "0:04 left"
void gainLabel(char* out, size_t n, int db);           // "LOW" / "HIGH" / "+6 dB"
void isoForGain(char* out, size_t n, int db);          // "ISO 400" (ISO = 400 * 2^(dB/6))

} // namespace fmt
