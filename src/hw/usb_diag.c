#include "usb_diag.h"

#include <hardware/structs/usb.h>
#include <stdio.h>
#include <tusb.h>

static uint32_t next_ms_;
static uint32_t last_status_ = 0xffffffff;

void tud_mount_cb(void) { printf("usb: mounted (configured by host)\n"); }
void tud_umount_cb(void) { printf("usb: unmounted\n"); }
void tud_suspend_cb(bool remote_wakeup_en) { printf("usb: suspend (remote wakeup %d)\n", remote_wakeup_en); }
void tud_resume_cb(void) { printf("usb: resume\n"); }

void usb_diag_poll(uint32_t now_ms) {
    if (tud_ready()) return;
    if ((int32_t)(now_ms - next_ms_) < 0) return;
    next_ms_ = now_ms + 1000;
    uint32_t s = usb_hw->sie_status;
    // only the stable bits; transfer/error flags are cleared by the driver
    uint32_t st = s & (USB_SIE_STATUS_VBUS_DETECTED_BITS | USB_SIE_STATUS_LINE_STATE_BITS |
                       USB_SIE_STATUS_SUSPENDED_BITS | USB_SIE_STATUS_SPEED_BITS | USB_SIE_STATUS_CONNECTED_BITS);
    printf("usb: not ready: vbus=%d connected=%d suspended=%d line=%d speed=%d addr=%d pullup=%d (sie 0x%08lx)%s\n",
           !!(s & USB_SIE_STATUS_VBUS_DETECTED_BITS), !!(s & USB_SIE_STATUS_CONNECTED_BITS),
           !!(s & USB_SIE_STATUS_SUSPENDED_BITS), (int)((s & USB_SIE_STATUS_LINE_STATE_BITS) >> 2),
           (int)((s & USB_SIE_STATUS_SPEED_BITS) >> 8), (int)(usb_hw->dev_addr_ctrl & 0x7f),
           !!(usb_hw->sie_ctrl & USB_SIE_CTRL_PULLUP_EN_BITS), (unsigned long)s, st == last_status_ ? "" : " *");
    last_status_ = st;
}
