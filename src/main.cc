// Touch controller for the Blackmagic Micro Studio Camera 4K G2.
// Waveshare RP2040 Touch LCD 1.28: USB-C to the camera (USB network, REST
// API), round LCD, physical REC / AF buttons. See README.md.

extern "C" {
#include <hardware/watchdog.h>
#include <pico/stdlib.h>

#include "dhcpserver/dhcpserver.h"
#include "log.h"
#include "usb_diag.h"
#include "usb_network.h"
}

#include <cstdio>

#include "buttons.h"
#include "camera.h"
#include "config.h"
#include "gesture.h"
#include "gfx.h"
#include "lcd.h"
#include "touch.h"
#include "ui.h"

static constexpr unsigned kLogPin = 27; // to the RX of a USB-UART converter
static constexpr unsigned kLogBaud = 921600;

static const ip4_addr_t kOwnIp = IPADDR4_INIT_BYTES(10, 0, 7, 5);
static const ip4_addr_t kNetmask = IPADDR4_INIT_BYTES(255, 255, 255, 0);
static const ip4_addr_t kGateway = IPADDR4_INIT_BYTES(0, 0, 0, 0);

static constexpr uint32_t kWatchdogMs = 2000;
static constexpr uint32_t kRedrawMs = 1000; // redraw at least this often
static constexpr uint32_t kTouchPollMs = 15;

static uint16_t framebuffer[gfx::W * gfx::H];
static Camera camera;
static ui::Ui screen;
static gesture::Recognizer gestures;
static dhcp_server_t dhcp;

static void onLease(void*, const uint8_t mac[6], const uint8_t ip[4]) {
    (void)mac;
    camera.onLease(ip);
}

static uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

int main() {
    const bool byWatchdog = watchdog_enable_caused_reboot();
    log_init(kLogPin, kLogBaud);
    printf("\ntouchcontrol %s %s, %s\n", __DATE__, __TIME__, byWatchdog ? "WATCHDOG REBOOT" : "power on");

    // USB first: the camera powers the board and only looks for the device
    // shortly after power-up. Attaching after the ~0.3 s LCD init was too late
    // (camera never configured us, "NO CAMERA" forever).
    if (!usb_network_init(&kOwnIp, &kNetmask, &kGateway)) {
        panic("usb_network_init");
    }
    dhcp_server_init(&dhcp, (ip_addr_t*)&kOwnIp, (ip_addr_t*)&kNetmask, false);
    dhcp.lease_cb = onLease;

    buttons::init();

    // keep USB serviced during the panel's reset/sleep-out delays
    gfx::Canvas canvas(framebuffer);
    lcd::init(usb_network_update);
    screen.draw(canvas, camera.state(), nowMs());
    lcd::push(framebuffer);
    while (lcd::busy()) usb_network_update();
    lcd::backlight(80);
    touch::init();

    watchdog_enable(kWatchdogMs, true);

    uint32_t drawnSeq = camera.state().seq;
    uint32_t lastDraw = nowMs();
    bool dirty = false;
    uint32_t nextTouch = 0;
    bool touchWasDown = false;
    int touchLastX = 0, touchLastY = 0, touchSamples = 0;

    for (;;) {
        watchdog_update();
        const uint32_t now = nowMs();

        usb_network_update();
        usb_diag_poll(now);
        camera.setLink(usb_network_is_up(), now);
        camera.poll(now);

        buttons::Events ev = buttons::poll(now);
        if (ev.pressed & (1u << buttons::REC)) camera.toggleRecord(now);
        if (ev.pressed & (1u << buttons::AF)) camera.autoFocus();
        if (ev.pressed & (1u << buttons::SPARE1)) printf("button: spare1 (GP%u)\n", buttons::kPins[buttons::SPARE1]);
        if (ev.pressed & (1u << buttons::SPARE2)) printf("button: spare2 (GP%u)\n", buttons::kPins[buttons::SPARE2]);

        if ((int32_t)(now - nextTouch) >= 0) {
            nextTouch = now + kTouchPollMs;
            touch::Sample t = touch::read();
            if (TOUCH_LOG && t.down && !touchWasDown) printf("touch: down %d,%d\n", t.x, t.y);
            if (TOUCH_LOG && !t.down && touchWasDown)
                printf("touch: up, last %d,%d after %d samples\n", touchLastX, touchLastY, touchSamples);
            if (t.down) {
                touchSamples = touchWasDown ? touchSamples + 1 : 1;
                touchLastX = t.x;
                touchLastY = t.y;
            }
            touchWasDown = t.down;
            gesture::Event g = gestures.update(t.down, t.x, t.y, now);
            if (g.type != gesture::Type::None) {
                if (TOUCH_LOG && g.type != gesture::Type::Repeat) printf("touch: gesture %d at %d,%d\n", (int)g.type, g.x, g.y);
                if (screen.onGesture(g, camera.state(), camera, now)) dirty = true;
            }
        }
        if (screen.tick(now)) dirty = true;

        if (camera.state().seq != drawnSeq) dirty = true;
        if (now - lastDraw >= kRedrawMs) dirty = true;
        // draw only when the previous frame has left the framebuffer
        if (dirty && !lcd::busy()) {
            drawnSeq = camera.state().seq;
            const uint64_t t0 = time_us_64();
            screen.draw(canvas, camera.state(), now);
            const uint32_t drawUs = (uint32_t)(time_us_64() - t0);
            if (drawUs > 20000) printf("ui: slow redraw %lu ms (screen %d)\n", (unsigned long)(drawUs / 1000), (int)screen.screen());
            lcd::push(framebuffer);
            lastDraw = now;
            dirty = false;
        }
    }
}
