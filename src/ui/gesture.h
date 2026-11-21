// Turns touch samples (finger down/up + position, polled every ~15 ms) into
// UI events. Pure logic, unit-tested on the host.
//
//   Tap     released quickly without moving (fires on release)
//   Swipe   moved more than kSwipePx, fires on release, dominant axis
//   Repeat  finger held still on the same spot: first after kHoldMs, then
//           every kRepeatMs (for +/- buttons; a held press gives no Tap)
#pragma once

#include <cstdint>

namespace gesture {

enum class Type : uint8_t { None, Tap, Repeat, SwipeLeft, SwipeRight, SwipeUp, SwipeDown };

struct Event {
    Type type = Type::None;
    int x = 0, y = 0; // where the finger went down
};

class Recognizer {
public:
    static constexpr int kMovePx = 18;    // more than this = not a tap
    static constexpr int kSwipePx = 40;
    static constexpr uint32_t kHoldMs = 450;
    static constexpr uint32_t kRepeatMs = 130;

    Event update(bool down, int x, int y, uint32_t nowMs);

private:
    bool down_ = false;
    bool moved_ = false;
    bool repeating_ = false;
    int x0_ = 0, y0_ = 0, lastX_ = 0, lastY_ = 0;
    uint32_t t0_ = 0, nextRepeat_ = 0;
};

} // namespace gesture
