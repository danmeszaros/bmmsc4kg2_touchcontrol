#include "lcd.h"

#include <initializer_list>

extern "C" {
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/spi.h>
#include <pico/stdlib.h>
}

// pins from the SDK board header (waveshare_rp2040_touch_lcd_1.28.h)
#define LCD_SPI spi1
static constexpr uint PIN_DC = WAVESHARE_LCD_DC_PIN;     // 8
static constexpr uint PIN_CS = WAVESHARE_LCD_CS_PIN;     // 9
static constexpr uint PIN_SCK = WAVESHARE_LCD_SCLK_PIN;  // 10
static constexpr uint PIN_MOSI = WAVESHARE_LCD_TX_PIN;   // 11
static constexpr uint PIN_RST = WAVESHARE_LCD_RST_PIN;   // 12
static constexpr uint PIN_BL = WAVESHARE_LCD_BL_PIN;     // 25

static constexpr uint32_t kSpiHz = 62500000; // clk_peri / 2

namespace lcd {

static int dma_ = -1;
static bool inFlight_ = false;

static void cmd(uint8_t c) {
    gpio_put(PIN_DC, 0);
    gpio_put(PIN_CS, 0);
    spi_write_blocking(LCD_SPI, &c, 1);
    gpio_put(PIN_CS, 1);
}

static void data(const uint8_t* d, size_t n) {
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);
    spi_write_blocking(LCD_SPI, d, n);
    gpio_put(PIN_CS, 1);
}

static void reg(uint8_t c, std::initializer_list<uint8_t> d) {
    cmd(c);
    if (d.size()) data(d.begin(), d.size());
}

void init() {
    spi_init(LCD_SPI, kSpiHz);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    for (uint p : {PIN_DC, PIN_CS, PIN_RST}) {
        gpio_init(p);
        gpio_set_dir(p, GPIO_OUT);
        gpio_put(p, 1);
    }

    gpio_set_function(PIN_BL, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_BL);
    pwm_set_wrap(slice, 100);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(PIN_BL), 0);
    pwm_set_enabled(slice, true);

    // hardware reset
    gpio_put(PIN_RST, 1);
    sleep_ms(20);
    gpio_put(PIN_RST, 0);
    sleep_ms(20);
    gpio_put(PIN_RST, 1);
    sleep_ms(120);

    // register sequence from Waveshare's LCD_1in28.c (GC9A01)
    cmd(0xEF);
    reg(0xEB, {0x14});
    cmd(0xFE);
    cmd(0xEF);
    reg(0xEB, {0x14});
    reg(0x84, {0x40});
    reg(0x85, {0xFF});
    reg(0x86, {0xFF});
    reg(0x87, {0xFF});
    reg(0x88, {0x0A});
    reg(0x89, {0x21});
    reg(0x8A, {0x00});
    reg(0x8B, {0x80});
    reg(0x8C, {0x01});
    reg(0x8D, {0x01});
    reg(0x8E, {0xFF});
    reg(0x8F, {0xFF});
    reg(0xB6, {0x00, 0x20});
    reg(0x36, {0x08}); // MADCTL: BGR, normal orientation
    reg(0x3A, {0x05}); // 16 bit/pixel
    reg(0x90, {0x08, 0x08, 0x08, 0x08});
    reg(0xBD, {0x06});
    reg(0xBC, {0x00});
    reg(0xFF, {0x60, 0x01, 0x04});
    reg(0xC3, {0x13});
    reg(0xC4, {0x13});
    reg(0xC9, {0x22});
    reg(0xBE, {0x11});
    reg(0xE1, {0x10, 0x0E});
    reg(0xDF, {0x21, 0x0C, 0x02});
    reg(0xF0, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A});
    reg(0xF1, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F});
    reg(0xF2, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A});
    reg(0xF3, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F});
    reg(0xED, {0x1B, 0x0B});
    reg(0xAE, {0x77});
    reg(0xCD, {0x63});
    reg(0x70, {0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03});
    reg(0xE8, {0x34});
    reg(0x62, {0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70});
    reg(0x63, {0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70});
    reg(0x64, {0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07});
    reg(0x66, {0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00});
    reg(0x67, {0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98});
    reg(0x74, {0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00});
    reg(0x98, {0x3E, 0x07});
    cmd(0x35); // tearing effect line on
    cmd(0x21); // inversion on (this panel needs it)
    cmd(0x11); // sleep out
    sleep_ms(120);
    cmd(0x29); // display on
    sleep_ms(20);

    dma_ = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_dreq(&c, spi_get_dreq(LCD_SPI, true));
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure(dma_, &c, &spi_get_hw(LCD_SPI)->dr, nullptr, 0, false);
}

void backlight(uint8_t percent) {
    if (percent > 100) percent = 100;
    pwm_set_chan_level(pwm_gpio_to_slice_num(PIN_BL), pwm_gpio_to_channel(PIN_BL), percent);
}

bool busy() {
    if (!inFlight_) return false;
    if (dma_channel_is_busy(dma_) || spi_is_busy(LCD_SPI)) return true;
    gpio_put(PIN_CS, 1);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    inFlight_ = false;
    return false;
}

bool push(const uint16_t* fb) {
    if (busy()) return false;
    reg(0x2A, {0x00, 0x00, 0x00, 0xEF}); // columns 0..239
    reg(0x2B, {0x00, 0x00, 0x00, 0xEF}); // rows 0..239
    cmd(0x2C);                           // memory write
    // 16 bit SPI frames: the RGB565 words go out MSB first as the panel wants
    spi_set_format(LCD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_put(PIN_DC, 1);
    gpio_put(PIN_CS, 0);
    dma_channel_set_read_addr(dma_, fb, false);
    dma_channel_set_trans_count(dma_, 240 * 240, true);
    inFlight_ = true;
    return true;
}

} // namespace lcd
