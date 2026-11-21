// USB ethernet (RNDIS + CDC-ECM, TinyUSB) <-> lwIP glue, NO_SYS.
//
// Based on code by Peter Lawrence (2020) and Matthew Bennett (2024), MIT license,
// influenced by lrndis https://github.com/fetisov/lrndis

#include <lwip/etharp.h>
#include <lwip/init.h>
#include <lwip/ip.h>
#include <lwip/timeouts.h>
#include <netif/ethernet.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>
#include <stdio.h>
#include <string.h>
#include <tusb.h>

#include "usb_network.h"

// TinyUSB reads this as the host-side (camera-side) MAC address
uint8_t tud_network_mac_address[6];

static struct netif netif_usb;
static bool netif_added = false;

// one received frame, handed from tud_network_recv_cb() to service_traffic()
static struct pbuf *received_frame;

// how long linkoutput may spin waiting for the IN endpoint before dropping
#define USB_TX_WAIT_US 20000

usb_net_stats_t usb_net_stats;

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static err_t linkoutput(struct netif *netif, struct pbuf *p) {
  (void)netif;
  absolute_time_t deadline = make_timeout_time_us(USB_TX_WAIT_US);
  for (;;) {
    if (!tud_ready()) {
      ++usb_net_stats.tx_fail;
      return ERR_IF;
    }
    if (tud_network_can_xmit(p->tot_len)) {
      tud_network_xmit(p, 0);
      ++usb_net_stats.tx_frames;
      return ERR_OK;
    }
    if (time_reached(deadline)) {
      // host is not reading; drop instead of hanging the main loop
      ++usb_net_stats.tx_fail;
      return ERR_MEM;
    }
    // let TinyUSB finish the previous transfer. This may call
    // tud_network_recv_cb(), which only stores a frame (no lwIP re-entry).
    tud_task();
  }
}

static err_t netif_init_cb(struct netif *netif) {
  netif->mtu = CFG_TUD_NET_MTU;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
  netif->state = NULL;
  netif->name[0] = 'E';
  netif->name[1] = 'X';
  netif->linkoutput = linkoutput;
  netif->output = etharp_output;
  return ERR_OK;
}

/* TinyUSB network driver callbacks --------------------------------------- */

void tud_network_init_cb(void) {
  printf("usb: network interface initialised by host\n");
  // (re)initialisation by the host: drop a leftover frame
  if (received_frame) {
    pbuf_free(received_frame);
    received_frame = NULL;
  }
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  // Returning false makes TinyUSB re-arm the OUT endpoint itself (frame
  // dropped). Returning true means we own the buffer and must call
  // tud_network_recv_renew() later - so true is returned only when a frame
  // was really stored, otherwise USB RX would stall forever.
  ++usb_net_stats.rx_frames;
  if (size >= 14) {
    uint16_t type = (uint16_t)(src[12] << 8 | src[13]);
    if (type == 0x0800) {
      ++usb_net_stats.rx_ipv4;
      if (size >= 38) {
        usb_net_stats.ip_src = be32(src + 26);
        usb_net_stats.ip_dst = be32(src + 30);
        unsigned ihl = (src[14] & 0x0f) * 4u;
        uint32_t dport = (size >= 14 + ihl + 4) ? be16(src + 14 + ihl + 2) : 0;
        usb_net_stats.ip_proto_dport = (uint32_t)src[23] << 16 | dport;
      }
    }
    else if (type == 0x0806) {
      ++usb_net_stats.rx_arp;
      if (size >= 42) {
        usb_net_stats.arp_op = be16(src + 20);
        usb_net_stats.arp_spa = be32(src + 28);
        usb_net_stats.arp_tpa = be32(src + 38);
      }
    }
    else if (type == 0x86DD) ++usb_net_stats.rx_ipv6;
    else { ++usb_net_stats.rx_other; usb_net_stats.last_other_type = type; }
  }
  if (received_frame || size == 0) {
    ++usb_net_stats.rx_dropped;
    return false;
  }

  struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
  if (!p) {
    ++usb_net_stats.rx_dropped;
    return false; // out of pbufs
  }
  pbuf_take(p, src, size); // PBUF_POOL may be a chain

  received_frame = p;
  return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  (void)arg;
  struct pbuf *p = (struct pbuf *)ref;
  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

/* API -------------------------------------------------------------------- */

void usb_network_update(void) {
  tud_task();

  if (received_frame) {
    struct pbuf *p = received_frame;
    received_frame = NULL;
    if (netif_usb.input(p, &netif_usb) != ERR_OK) {
      pbuf_free(p);
    }
    tud_network_recv_renew();
  }

  sys_check_timeouts();
}

bool usb_network_is_up(void) {
  return tud_ready();
}

bool usb_network_init(const ip4_addr_t *ownip, const ip4_addr_t *netmask, const ip4_addr_t *gateway) {
  if (!tud_init(PICO_TUD_RHPORT)) {
    return false;
  }

  lwip_init();

  // locally administered unicast MAC derived from the board id
  pico_unique_board_id_t board_id;
  pico_get_unique_board_id(&board_id);
  memcpy(tud_network_mac_address, &board_id.id[2], 6);
  tud_network_mac_address[0] &= (uint8_t)~0x1;
  tud_network_mac_address[0] |= 0x2;

  // our (lwIP) MAC must differ from the host-side one
  netif_usb.hwaddr_len = sizeof(tud_network_mac_address);
  memcpy(netif_usb.hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
  netif_usb.hwaddr[5] ^= 0x01;

  if (netif_add(&netif_usb, ownip, netmask, gateway, NULL, netif_init_cb, ethernet_input) == NULL) {
    return false;
  }
  netif_set_default(&netif_usb);
  netif_added = true;
  return true;
}

void usb_network_deinit(void) {
  if (netif_added) {
    netif_remove(&netif_usb);
    netif_added = false;
  }
  tud_deinit(PICO_TUD_RHPORT);
}
