// USB ethernet (RNDIS + CDC-ECM, TinyUSB) <-> lwIP glue, NO_SYS.
// MIT license, see usb_network.c

#ifndef USB_NETWORK_H
#define USB_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lwip/ip4_addr.h>
#include <stdint.h>
#include <stdbool.h>

// initialises TinyUSB, lwIP and the USB netif (set as default netif)
bool usb_network_init(const ip4_addr_t *ownip, const ip4_addr_t *netmask, const ip4_addr_t *gateway);
// true while the host (camera) has configured the USB device
bool usb_network_is_up(void);
// call as often as possible from the main loop
void usb_network_update(void);
void usb_network_deinit(void);

// diagnostics, read by the STATUS message
typedef struct {
  uint32_t rx_frames;      // frames from the camera
  uint32_t rx_ipv4, rx_arp, rx_ipv6, rx_other;
  uint32_t rx_dropped;     // not accepted (previous frame pending / no pbuf)
  uint32_t tx_frames;      // frames handed to TinyUSB
  uint32_t tx_fail;        // linkoutput: not ready / IN endpoint timeout
  uint32_t dhcp_discover, dhcp_request, dhcp_reply;
  uint16_t last_other_type; // EtherType of the last rx_other frame
  // last ARP: op, sender/target IP; last IPv4: src/dst IP, proto<<16 | dst port
  // (IPs as wire bytes packed big-endian into the u32)
  uint32_t arp_op, arp_spa, arp_tpa;
  uint32_t ip_src, ip_dst, ip_proto_dport;
} usb_net_stats_t;
extern usb_net_stats_t usb_net_stats;

#ifdef __cplusplus
}
#endif

#endif // USB_NETWORK_H
