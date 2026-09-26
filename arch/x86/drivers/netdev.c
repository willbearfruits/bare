#include "netdev.h"
#include "platform.h"
#include "libc.h"
#include "log.h"

#define NETDEVS 4
static struct netdev devs[NETDEVS];

struct netdev *netdev_add(void) {
    for (int i = 0; i < NETDEVS; i++) if (!devs[i].used) { memset(&devs[i], 0, sizeof devs[i]); devs[i].used = true; return &devs[i]; }
    return 0;
}
void netdev_remove(struct netdev *n) { if (n) n->used = false; }
void net_ports_init(void) {
    static bool done;
    if (done) return;
    done = true;
    e1000_init();
}

static struct netdev *cur(void) {                  /* the port in use: one with a link, else the first */
    struct netdev *first = 0;
    for (int i = 0; i < NETDEVS; i++) {
        if (!devs[i].used) continue;
        if (!first) first = &devs[i];
        if (devs[i].link(&devs[i])) return &devs[i];
    }
    return first;
}
bool plat_net_present(uint8_t mac[6]) { struct netdev *n = cur(); if (n) memcpy(mac, n->mac, 6); return n != 0; }
bool plat_net_link(void) { struct netdev *n = cur(); return n && n->link(n); }
bool plat_net_send(const void *f, int len) { struct netdev *n = cur(); return n && n->send(n, f, len); }
int  plat_net_recv(void *f, int cap) { struct netdev *n = cur(); return n ? n->recv(n, f, cap) : 0; }
void plat_net_multicast(bool all) { for (int i = 0; i < NETDEVS; i++) if (devs[i].used && devs[i].multicast) devs[i].multicast(&devs[i], all); }
