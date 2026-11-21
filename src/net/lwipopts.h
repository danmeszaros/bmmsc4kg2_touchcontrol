#ifndef LWIPOPTS_H
#define LWIPOPTS_H

// lwIP configuration for the USB-ethernet camera proxy.
// See https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define MEM_LIBC_MALLOC 0
#define MEM_ALIGNMENT 4

// heap for TCP segments (tcp_write with COPY) and misc
#define MEM_SIZE 16000

// TCP
#define LWIP_TCP 1
#define TCP_MSS 1460
#define TCP_WND (8 * TCP_MSS)
#define TCP_SND_BUF (8 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define MEMP_NUM_TCP_SEG 32
// one active connection at a time, but closed ones linger in
// FIN_WAIT/TIME_WAIT (2*MSL = 120 s); lwIP recycles the oldest TIME_WAIT pcb
// when it runs out. Kept low on purpose so pcb pressure is observable.
#define MEMP_NUM_TCP_PCB 8
#define LWIP_TCP_KEEPALIVE 0

// pbufs: RX frames from USB are allocated from the pool
#define PBUF_POOL_SIZE 24

// IP / link
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 0
#define LWIP_UDP 1 // DHCP server
#define LWIP_DHCP 0
// DHCP server: DISCOVER/REQUEST come from 0.0.0.0, which ip4_input() drops
// as a "broadcast source" unless link-layer addressing is enabled. LWIP_DHCP=1
// would enable it for the client port only; this enables it for port 67.
// Without it the camera never gets a lease and falls back to 169.254.x.x.
#define LWIP_IP_ACCEPT_UDP_PORT(port) ((port) == PP_NTOHS(67))
#define LWIP_DNS 0
#define LWIP_IGMP 0
#define MEMP_NUM_ARP_QUEUE 10
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_NETIF_STATUS_CALLBACK 0
#define LWIP_NETIF_LINK_CALLBACK 0
#define LWIP_NETIF_HOSTNAME 0
#define LWIP_NETIF_TX_SINGLE_PBUF 1
#define LWIP_CHKSUM_ALGORITHM 3

// statistics are read by the STATUS message (no debug output)
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 0
#define MEM_STATS 1
#define MEMP_STATS 1
#define LINK_STATS 0
#define ETHARP_STATS 0
#define IP_STATS 0
#define ICMP_STATS 0
#define UDP_STATS 0
#define TCP_STATS 1
#define SYS_STATS 0

#endif // LWIPOPTS_H
