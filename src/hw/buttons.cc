#include "buttons.h"

extern "C" {
#include <hardware/gpio.h>
}

namespace buttons {

static constexpr uint32_t kDebounceMs = 20;

static bool stable_[COUNT];      // debounced state, true = down
static bool raw_[COUNT];
static uint32_t rawSince_[COUNT];

void init() {
    for (int i = 0; i < COUNT; ++i) {
        gpio_init(kPins[i]);
        gpio_set_dir(kPins[i], GPIO_IN);
        gpio_pull_up(kPins[i]);
        stable_[i] = raw_[i] = false;
        rawSince_[i] = 0;
    }
}

Events poll(uint32_t nowMs) {
    Events ev;
    for (int i = 0; i < COUNT; ++i) {
        bool down = !gpio_get(kPins[i]);
        if (down != raw_[i]) {
            raw_[i] = down;
            rawSince_[i] = nowMs;
        } else if (down != stable_[i] && nowMs - rawSince_[i] >= kDebounceMs) {
            stable_[i] = down;
            if (down) ev.pressed |= 1u << i;
            else ev.released |= 1u << i;
        }
        if (stable_[i]) ev.held |= 1u << i;
    }
    return ev;
}

} // namespace buttons
