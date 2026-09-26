#pragma once
/* The network: the first Ethernet port the platform has, IPv4 on it. ARP, ICMP echo (it answers ping), UDP, IGMP
   for multicast groups, and an address from DHCP — or, when no DHCP server answers (two computers on one cable), a
   link-local one (169.254.x.y, RFC 3927: probed with ARP first). Polled from the main loop; nothing is fragmented or
   reassembled (the datagrams used here are small). Addresses are in host order. */
#include <stdint.h>
#include <stdbool.h>

enum { NET_NONE, NET_NO_LINK, NET_DHCP, NET_PROBING, NET_READY };
struct net_state {
    int phase;
    bool link_local;                           /* the address is 169.254.x.y, picked here */
    uint8_t mac[6];
    uint32_t ip, mask, gw, dhcp_server, lease_s;
    uint32_t rx, tx, dropped;                  /* frames */
    char status[80];
};
extern struct net_state net;

typedef void (*udp_handler)(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, const uint8_t *data, int len, uint64_t now);
void net_init(void);
void net_work(uint64_t now);                   /* the main loop: frames in, timers (DHCP, ARP, IGMP) */
bool net_listen(uint16_t port, udp_handler h); /* UDP to this port, unicast or multicast */
bool net_send(uint32_t dst, uint16_t src_port, uint16_t dst_port, const void *data, int len);   /* false: not now (no link, ARP pending) */
bool net_join(uint32_t group);                 /* receive this multicast group (and tell the switch: IGMP) */
void net_ip_str(uint32_t ip, char *out, int cap);
#define NET_IP(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
