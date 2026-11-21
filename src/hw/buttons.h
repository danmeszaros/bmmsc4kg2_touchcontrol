// Physical buttons: to GND, internal pull-up, debounced.
#pragma once

#include <cstdint>

namespace buttons {

enum Id : uint8_t { REC = 0, AF = 1, SPARE1 = 2, SPARE2 = 3, COUNT = 4 };

// GPIOs available on the board header: 16, 17, 18, 26
constexpr unsigned kPins[COUNT] = {16, 17, 18, 26};

struct Events {
    uint8_t pressed = 0;  // bit per Id: went down since the last poll
    uint8_t released = 0; // bit per Id: went up since the last poll
    uint8_t held = 0;     // bit per Id: currently down
};

void init();
Events poll(uint32_t nowMs);

} // namespace buttons
