#include "log.h"

#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <pico/stdio.h>
#include <pico/stdio/driver.h>

#include "uart_tx.pio.h"

static PIO pio_ = pio0;
static uint sm_;

static void out_chars(const char* buf, int len) {
    for (int i = 0; i < len; ++i) {
        pio_sm_put_blocking(pio_, sm_, (uint32_t)(uint8_t)buf[i]);
    }
}

static stdio_driver_t driver_ = {
    .out_chars = out_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = true,
#endif
};

void log_init(unsigned txPin, unsigned baud) {
    sm_ = (uint)pio_claim_unused_sm(pio_, true);
    uint off = pio_add_program(pio_, &uart_tx_program);
    pio_sm_set_pins_with_mask(pio_, sm_, 1u << txPin, 1u << txPin);
    pio_sm_set_pindirs_with_mask(pio_, sm_, 1u << txPin, 1u << txPin);
    pio_gpio_init(pio_, txPin);
    pio_sm_config c = uart_tx_program_get_default_config(off);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_out_pins(&c, txPin, 1);
    sm_config_set_sideset_pins(&c, txPin);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (8.0f * (float)baud));
    pio_sm_init(pio_, sm_, off, &c);
    pio_sm_set_enabled(pio_, sm_, true);
    stdio_set_driver_enabled(&driver_, true);
}
