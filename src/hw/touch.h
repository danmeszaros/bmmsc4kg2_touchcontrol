// CST816S capacitive touch (I2C1: SDA GP6, SCL GP7, INT GP21, RST GP22,
// address 0x15) in point mode: reports one finger position, gestures are
// recognised in software (ui/gesture.h). Coordinates are returned in screen
// space (config.h: rotation and axis mapping applied).
#pragma once

#include <cstdint>

namespace touch {

struct Sample {
    bool down = false;
    int x = 0, y = 0;
};

bool init(); // false if the chip does not answer
Sample read();

} // namespace touch
