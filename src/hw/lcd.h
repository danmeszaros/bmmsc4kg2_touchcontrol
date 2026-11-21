// GC9A01 240x240 round LCD on the Waveshare RP2040 Touch LCD 1.28 (SPI1).
// The framebuffer is sent with DMA in the background: start push(), keep
// running the main loop, and do not touch the framebuffer while busy().
#pragma once

#include <cstdint>

namespace lcd {

// `idle` is called while init waits for the panel (~300 ms), so USB keeps
// being serviced; may be null
void init(void (*idle)());
void backlight(uint8_t percent);
// starts sending the whole 240x240 RGB565 frame; false if still busy
bool push(const uint16_t* fb);
bool busy(); // also finishes a completed transfer (CS high, SPI back to 8 bit)

} // namespace lcd
