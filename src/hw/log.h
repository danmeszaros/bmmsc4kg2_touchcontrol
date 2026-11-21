// printf() goes to a PIO UART transmitter (debug log, 921600 8N1 by default).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void log_init(unsigned txPin, unsigned baud);

#ifdef __cplusplus
}
#endif
