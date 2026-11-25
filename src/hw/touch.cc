#include "touch.h"

extern "C" {
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <pico/stdlib.h>
}

#include <cstdio>

#include "config.h"

// pins and registers from Waveshare's DEV_Config.h / CST816S.h
#define TOUCH_I2C i2c1
static constexpr uint PIN_SDA = 6;
static constexpr uint PIN_SCL = 7;
static constexpr uint PIN_INT = 21;
static constexpr uint PIN_RST = 22;
static constexpr uint8_t ADDR = 0x15;

enum : uint8_t {
    REG_GESTURE = 0x01, // 0x01..0x06: gesture, fingers, XH, XL, YH, YL
    REG_CHIP_ID = 0xA7,
    REG_FW_VERSION = 0xA9,
    REG_IRQ_PULSE_WIDTH = 0xED,
    REG_NOR_SCAN_PER = 0xEE,
    REG_IRQ_CTL = 0xFA,
    REG_DIS_AUTO_SLEEP = 0xFE,
};

namespace touch {

static bool ok_ = false;

static bool writeReg(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    return i2c_write_timeout_us(TOUCH_I2C, ADDR, b, 2, false, 2000) == 2;
}

static bool readRegs(uint8_t reg, uint8_t* out, size_t n) {
    if (i2c_write_timeout_us(TOUCH_I2C, ADDR, &reg, 1, true, 2000) != 1) return false;
    return i2c_read_timeout_us(TOUCH_I2C, ADDR, out, n, false, 2000) == (int)n;
}

bool init() {
    i2c_init(TOUCH_I2C, 400 * 1000);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);
    gpio_init(PIN_INT);
    gpio_set_dir(PIN_INT, GPIO_IN);
    gpio_pull_up(PIN_INT);
    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);

    gpio_put(PIN_RST, 0);
    sleep_ms(10);
    gpio_put(PIN_RST, 1);
    sleep_ms(50);

    uint8_t id = 0, fw = 0;
    if (!readRegs(REG_CHIP_ID, &id, 1)) {
        printf("touch: no answer on I2C\n");
        return false;
    }
    readRegs(REG_FW_VERSION, &fw, 1);
    printf("touch: CST816 chip id 0x%02x fw %u\n", id, fw);
    // always awake (otherwise it stops answering I2C after a few seconds),
    // point mode: position updates while a finger is down
    ok_ = writeReg(REG_DIS_AUTO_SLEEP, 0x01) && writeReg(REG_IRQ_CTL, 0x41) &&
          writeReg(REG_IRQ_PULSE_WIDTH, 0x01) && writeReg(REG_NOR_SCAN_PER, 0x01);
    if (!ok_) printf("touch: config write failed\n");
    return ok_;
}

// raw panel coordinates -> screen coordinates
static void map(int rx, int ry, int& x, int& y) {
    int px = rx, py = ry;
    if (TOUCH_SWAP_XY) { int t = px; px = py; py = t; }
    if (TOUCH_MIRROR_X) px = 239 - px;
    if (TOUCH_MIRROR_Y) py = 239 - py;
    switch (UI_ROTATION) {
    // must match MADCTL in lcd.cc: at 90 (MV|MX) screen (0,0) is the panel's top-right
    case 90: x = py; y = 239 - px; break;
    case 180: x = 239 - px; y = 239 - py; break;
    case 270: x = 239 - py; y = px; break;
    default: x = px; y = py; break;
    }
}

Sample read() {
    Sample s;
    if (!ok_) return s;
    uint8_t r[6];
    if (!readRegs(REG_GESTURE, r, sizeof r)) return s;
    s.down = (r[1] & 0x0F) != 0;
    int rx = ((r[2] & 0x0F) << 8) | r[3];
    int ry = ((r[4] & 0x0F) << 8) | r[5];
    if (rx > 239) rx = 239;
    if (ry > 239) ry = 239;
    map(rx, ry, s.x, s.y);
    return s;
}

} // namespace touch
