#include "buttons.h"

#include "config.h"

extern "C" {
#include <hardware/gpio.h>
#include <hardware/sync.h>
#include <pico/time.h>
}

// BUTTONS_MODE (config.h) picks one of three implementations behind the same
// init() / poll() interface.

namespace buttons {

static void setupPins() {
    for (int i = 0; i < COUNT; ++i) {
        gpio_init(kPins[i]);
        gpio_set_dir(kPins[i], GPIO_IN);
        gpio_pull_up(kPins[i]);
    }
}

// both edges of our pins to handler, through a raw handler for our pins only
// (leaves the shared GPIO callback free)
[[maybe_unused]] static void enableIrq(void (*handler)()) {
    uint32_t mask = 0;
    for (int i = 0; i < COUNT; ++i) mask |= 1u << kPins[i];
    gpio_add_raw_irq_handler_masked(mask, handler);
    for (int i = 0; i < COUNT; ++i) {
        gpio_acknowledge_irq(kPins[i], GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE);
        gpio_set_irq_enabled(kPins[i], GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    }
    irq_set_enabled(IO_IRQ_BANK0, true);
}

#if BUTTONS_MODE == 0
// --- 0: polled ----------------------------------------------------------------

static constexpr uint32_t kDebounceMs = 20;

static bool stable_[COUNT];      // debounced state, true = down
static bool raw_[COUNT];
static uint32_t rawSince_[COUNT];

void init() {
    setupPins();
    for (int i = 0; i < COUNT; ++i) {
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

#elif BUTTONS_MODE == 1
// --- 1: interrupt, old firmware's logic ------------------------------------
// Same as the Button class of bmmsc4kg2_control, which worked reliably with
// these switches:
// - the first falling edge is a press, at once (no waiting)
// - the button stays "down" until 50 ms have passed since the last rising
//   edge with no falling edge since; only then can it be pressed again
static constexpr uint32_t kDebounceMs = 50;

struct State {
    bool stableDown;      // press reported, release not confirmed yet
    bool eventPressed;    // press not yet handed to poll()
    bool eventReleased;   // rising edge seen, waiting for the debounce time
    uint32_t lastUpMs;    // time of the last rising edge
};

static volatile State st_[COUNT];

static uint32_t nowMs32() { return (uint32_t)(time_us_64() / 1000); }

static void onGpioIrq() {
    const uint32_t now = nowMs32();
    for (int i = 0; i < COUNT; ++i) {
        const uint32_t events = gpio_get_irq_event_mask(kPins[i]);
        if (!events) continue;
        gpio_acknowledge_irq(kPins[i], events);
        volatile State& s = st_[i];
        if (events & GPIO_IRQ_EDGE_FALL) {
            s.eventReleased = false;
            // released -> pressed
            if (!s.stableDown) {
                s.eventPressed = true;
                s.stableDown = true;
            }
        } else if (events & GPIO_IRQ_EDGE_RISE) {
            s.lastUpMs = now;
            s.eventReleased = true;
        }
    }
}

void init() {
    setupPins();
    for (int i = 0; i < COUNT; ++i) {
        st_[i].stableDown = st_[i].eventPressed = st_[i].eventReleased = false;
        st_[i].lastUpMs = 0;
    }
    enableIrq(onGpioIrq);
}

Events poll(uint32_t) {
    Events ev;
    const uint32_t irqs = save_and_disable_interrupts();
    const uint32_t now = nowMs32();
    for (int i = 0; i < COUNT; ++i) {
        volatile State& s = st_[i];
        if (s.eventPressed) {
            s.eventPressed = false;
            ev.pressed |= 1u << i;
        }
        // release confirmed once the contact has been quiet for the debounce time
        if (s.eventReleased && now - s.lastUpMs > kDebounceMs) {
            s.eventReleased = false;
            s.stableDown = false;
            ev.released |= 1u << i;
        }
        if (s.stableDown) ev.held |= 1u << i;
    }
    restore_interrupts(irqs);
    return ev;
}

#elif BUTTONS_MODE == 2
// --- 2: interrupt, level based -----------------------------------------------
// The first edge counts at once (caught even while the main loop is busy);
// further edges on that pin are ignored for kDebounceUs (contact bounce).
// poll() settles on the pin level once the window has passed, in case the
// last edge of a bounce burst was swallowed.

static constexpr uint32_t kDebounceUs = 20000;

static volatile bool down_[COUNT];         // debounced state, true = down
static volatile uint32_t lastEdgeUs_[COUNT];
static volatile uint8_t pendingPress_;
static volatile uint8_t pendingRelease_;

// caller has interrupts off (or is the interrupt)
static void edge(int i, bool down, uint32_t nowUs) {
    if (down == down_[i] || nowUs - lastEdgeUs_[i] < kDebounceUs) return;
    down_[i] = down;
    lastEdgeUs_[i] = nowUs;
    if (down) pendingPress_ |= 1u << i;
    else pendingRelease_ |= 1u << i;
}

static void onGpioIrq() {
    const uint32_t nowUs = time_us_32();
    for (int i = 0; i < COUNT; ++i) {
        const uint32_t ev = gpio_get_irq_event_mask(kPins[i]);
        if (!ev) continue;
        gpio_acknowledge_irq(kPins[i], ev);
        edge(i, !gpio_get(kPins[i]), nowUs);
    }
}

void init() {
    setupPins();
    const uint32_t nowUs = time_us_32();
    for (int i = 0; i < COUNT; ++i) {
        down_[i] = false;
        lastEdgeUs_[i] = nowUs - kDebounceUs;
    }
    pendingPress_ = pendingRelease_ = 0;
    enableIrq(onGpioIrq);
}

Events poll(uint32_t) {
    Events ev;
    const uint32_t irqs = save_and_disable_interrupts();
    const uint32_t nowUs = time_us_32();
    for (int i = 0; i < COUNT; ++i) edge(i, !gpio_get(kPins[i]), nowUs);
    ev.pressed = pendingPress_;
    ev.released = pendingRelease_;
    pendingPress_ = pendingRelease_ = 0;
    for (int i = 0; i < COUNT; ++i) {
        if (down_[i]) ev.held |= 1u << i;
    }
    restore_interrupts(irqs);
    return ev;
}

#else
#error "BUTTONS_MODE must be 0, 1 or 2"
#endif

} // namespace buttons
