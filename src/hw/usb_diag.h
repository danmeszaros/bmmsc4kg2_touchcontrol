// USB device diagnostics in the log: TinyUSB mount/suspend events and, while
// the host (camera) has not configured us, the raw SIE state once a second.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_diag_poll(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
