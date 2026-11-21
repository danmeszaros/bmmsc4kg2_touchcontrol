#include "gesture.h"

#include <cstdlib>

namespace gesture {

Event Recognizer::update(bool down, int x, int y, uint32_t nowMs) {
    Event ev;
    if (down && !down_) {
        down_ = true;
        moved_ = repeating_ = false;
        x0_ = lastX_ = x;
        y0_ = lastY_ = y;
        t0_ = nowMs;
        nextRepeat_ = nowMs + kHoldMs;
        return ev;
    }
    if (down) {
        lastX_ = x;
        lastY_ = y;
        if (std::abs(x - x0_) > kMovePx || std::abs(y - y0_) > kMovePx) moved_ = true;
        if (!moved_ && (int32_t)(nowMs - nextRepeat_) >= 0) {
            repeating_ = true;
            nextRepeat_ = nowMs + kRepeatMs;
            ev.type = Type::Repeat;
            ev.x = x0_;
            ev.y = y0_;
        }
        return ev;
    }
    if (down_) { // released
        down_ = false;
        ev.x = x0_;
        ev.y = y0_;
        const int dx = lastX_ - x0_, dy = lastY_ - y0_;
        if (std::abs(dx) >= kSwipePx || std::abs(dy) >= kSwipePx) {
            if (std::abs(dx) >= std::abs(dy)) ev.type = dx < 0 ? Type::SwipeLeft : Type::SwipeRight;
            else ev.type = dy < 0 ? Type::SwipeUp : Type::SwipeDown;
        } else if (!moved_ && !repeating_) {
            ev.type = Type::Tap;
        }
    }
    return ev;
}

} // namespace gesture
