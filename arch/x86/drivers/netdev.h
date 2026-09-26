#pragma once
/* Network ports: each driver (Intel PRO/1000 family, USB CDC Ethernet adapters) adds its ports here; the platform's
   plat_net_* use the first one present, preferring one with a link. */
#include <stdint.h>
#include <stdbool.h>

struct netdev {
    char name[40];
    uint8_t mac[6];
    bool used;
    bool (*link)(struct netdev *n);
    bool (*send)(struct netdev *n, const void *frame, int len);
    int  (*recv)(struct netdev *n, void *frame, int cap);
    void (*multicast)(struct netdev *n, bool all);
    void *ctx;
};
struct netdev *netdev_add(void);            /* 0 when full */
void netdev_remove(struct netdev *n);       /* a USB adapter unplugged */
void net_ports_init(void);                  /* the PCI ones (USB ones arrive with the USB stack) */
int  e1000_init(void);
