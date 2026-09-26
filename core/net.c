#include "net.h"
#include "libc.h"
#include "platform.h"
#include "log.h"

struct net_state net;

#define ETH_ARP 0x0806
#define ETH_IP  0x0800
enum { P_ICMP = 1, P_IGMP = 2, P_UDP = 17 };
static uint8_t tx[1536], rx[1536];
static const uint8_t bcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t sum16(const uint8_t *p, int n, uint32_t s) {
    for (int i = 0; i + 1 < n; i += 2) s += (uint32_t)(p[i] << 8 | p[i + 1]);
    if (n & 1) s += (uint32_t)p[n - 1] << 8;
    return s;
}
static uint16_t fold(uint32_t s) { while (s >> 16) s = (s & 0xFFFF) + (s >> 16); return (uint16_t)~s; }

void net_ip_str(uint32_t ip, char *out, int cap) { snfmt(out, cap, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255); }

/* ---- the port ---- */
static bool send_frame(const uint8_t *dst, uint16_t type, int payload) {
    memcpy(tx, dst, 6); memcpy(tx + 6, net.mac, 6); put16(tx + 12, type);
    int len = 14 + payload; if (len < 60) { memset(tx + len, 0, (size_t)(60 - len)); len = 60; }
    if (!plat_net_send(tx, len)) return false;
    net.tx++;
    return true;
}

/* ---- ARP: a small cache, and a few frames waiting for an answer ---- */
#define ARPS 16
static struct { uint32_t ip; uint8_t mac[6]; uint64_t when; } arp[ARPS];
#define PENDING 4
static struct { uint32_t ip; int len; uint8_t f[1536]; uint64_t asked; int tries; } pend[PENDING];
static uint64_t now_ms;

static void arp_send(uint16_t op, const uint8_t *tha, uint32_t spa, uint32_t tpa, const uint8_t *dst) {
    uint8_t *a = tx + 14;
    put16(a, 1); put16(a + 2, ETH_IP); a[4] = 6; a[5] = 4; put16(a + 6, op);
    memcpy(a + 8, net.mac, 6); put32(a + 14, spa); memcpy(a + 18, tha, 6); put32(a + 24, tpa);
    send_frame(dst, ETH_ARP, 28);
}
static const uint8_t *arp_find(uint32_t ip) {
    for (int i = 0; i < ARPS; i++) if (arp[i].ip == ip && arp[i].when && now_ms - arp[i].when < 300000) return arp[i].mac;
    return 0;
}
static void arp_learn(uint32_t ip, const uint8_t *mac) {
    if (!ip) return;
    int slot = 0;
    for (int i = 0; i < ARPS; i++) { if (arp[i].ip == ip) { slot = i; break; } if (arp[i].when < arp[slot].when) slot = i; }
    arp[slot].ip = ip; memcpy(arp[slot].mac, mac, 6); arp[slot].when = now_ms ? now_ms : 1;
    for (int p = 0; p < PENDING; p++)                                   /* frames that were waiting for it */
        if (pend[p].len && pend[p].ip == ip) {
            memcpy(pend[p].f, mac, 6);
            if (plat_net_send(pend[p].f, pend[p].len)) net.tx++;
            pend[p].len = 0;
        }
}
/* where an IPv4 datagram goes on the wire: multicast and broadcast have their own addresses; on the link, the host;
   elsewhere, the router */
static bool next_hop(uint32_t dst, uint8_t mac[6], uint32_t *ask) {
    *ask = 0;
    if ((dst >> 28) == 0xE) { mac[0] = 0x01; mac[1] = 0x00; mac[2] = 0x5E; mac[3] = (uint8_t)((dst >> 16) & 0x7F); mac[4] = (uint8_t)(dst >> 8); mac[5] = (uint8_t)dst; return true; }
    if (dst == 0xFFFFFFFFu || (net.mask && (dst | net.mask) == 0xFFFFFFFFu)) { memcpy(mac, bcast_mac, 6); return true; }
    uint32_t hop = net.link_local || !net.gw || (dst & net.mask) == (net.ip & net.mask) || (dst >> 16) == 0xA9FE ? dst : net.gw;
    const uint8_t *m = arp_find(hop);
    if (m) { memcpy(mac, m, 6); return true; }
    *ask = hop;
    return false;
}

/* ---- IPv4 out ---- */
static uint16_t ip_id;
/* the payload is already at tx + 14 + 20 (IGMP's header is 4 bytes longer: moved up for it) */
static bool ip_send(uint32_t src, uint32_t dst, uint8_t proto, int len, int ttl) {
    bool igmp = proto == P_IGMP;
    int hl = igmp ? 24 : 20;
    uint8_t *h = tx + 14;
    if (hl != 20) memmove(h + hl, h + 20, (size_t)len);                              /* callers build the payload after a 20-byte header */
    h[0] = (uint8_t)(0x40 | hl / 4); h[1] = igmp ? 0xC0 : 0; put16(h + 2, (uint16_t)(hl + len)); put16(h + 4, ++ip_id);
    put16(h + 6, 0x4000); h[8] = (uint8_t)ttl; h[9] = proto; put16(h + 10, 0); put32(h + 12, src); put32(h + 16, dst);
    if (igmp) { h[20] = 0x94; h[21] = 4; h[22] = 0; h[23] = 0; }        /* Router Alert */
    put16(h + 10, fold(sum16(h, hl, 0)));
    uint8_t mac[6]; uint32_t ask;
    if (next_hop(dst, mac, &ask)) return send_frame(mac, ETH_IP, hl + len);
    int p = 0;                                                         /* not known yet: ask, and keep the frame */
    for (int i = 0; i < PENDING; i++) if (!pend[i].len) { p = i; break; } else if (pend[i].asked < pend[p].asked) p = i;
    memcpy(tx, bcast_mac, 6); memcpy(tx + 6, net.mac, 6); put16(tx + 12, ETH_IP);
    int flen = MAX(14 + hl + len, 60);
    memcpy(pend[p].f, tx, (size_t)flen); pend[p].len = flen; pend[p].ip = ask; pend[p].asked = now_ms; pend[p].tries = 1;
    arp_send(1, (const uint8_t[6]){ 0 }, net.ip, ask, bcast_mac);
    return false;
}

static int udp_build(uint32_t src, uint32_t dst, uint16_t sport, uint16_t dport, const void *data, int len) {
    uint8_t *u = tx + 14 + 20;
    put16(u, sport); put16(u + 2, dport); put16(u + 4, (uint16_t)(8 + len)); put16(u + 6, 0);
    memmove(u + 8, data, (size_t)len);                                /* DHCP builds its payload in place */
    uint8_t ph[12]; put32(ph, src); put32(ph + 4, dst); ph[8] = 0; ph[9] = P_UDP; put16(ph + 10, (uint16_t)(8 + len));
    uint16_t c = fold(sum16(u, 8 + len, sum16(ph, 12, 0)));
    put16(u + 6, c ? c : 0xFFFF);
    return 8 + len;
}
bool net_send(uint32_t dst, uint16_t sport, uint16_t dport, const void *data, int len) {
    if (net.phase != NET_READY || len < 0 || len > 1472) return false;
    int n = udp_build(net.ip, dst, sport, dport, data, len);
    return ip_send(net.ip, dst, P_UDP, n, (dst >> 28) == 0xE ? 1 : 64);
}

/* ---- multicast groups ---- */
#define GROUPS 8
static uint32_t groups[GROUPS]; static int ngroups;
static uint64_t igmp_at;
static void igmp_report(uint32_t g) {
    uint8_t *m = tx + 14 + 20;
    m[0] = 0x16; m[1] = 0; put16(m + 2, 0); put32(m + 4, g);          /* IGMPv2 membership report */
    put16(m + 2, fold(sum16(m, 8, 0)));
    ip_send(net.ip, g, P_IGMP, 8, 1);
}
bool net_join(uint32_t g) {
    for (int i = 0; i < ngroups; i++) if (groups[i] == g) return true;
    if (ngroups == GROUPS) return false;
    groups[ngroups++] = g;
    plat_net_multicast(true);
    if (net.phase == NET_READY) igmp_report(g);
    return true;
}

/* ---- UDP in ---- */
#define PORTS 8
static struct { uint16_t port; udp_handler h; } ports[PORTS]; static int nports;
bool net_listen(uint16_t port, udp_handler h) {
    for (int i = 0; i < nports; i++) if (ports[i].port == port) { ports[i].h = h; return true; }
    if (nports == PORTS) return false;
    ports[nports].port = port; ports[nports].h = h; nports++;
    return true;
}

/* ---- DHCP, then link-local ---- */
static uint32_t xid, offered, lease_until;
static int dhcp_tries; static uint64_t dhcp_at, phase_at;
static uint32_t ll_try; static int probes;
static uint64_t link_at, look_at; static bool link;              /* the cable, as last seen; when a port was last looked for */

static void dhcp_send(int type) {
    uint8_t *b = tx + 14 + 20 + 8;
    memset(b, 0, 300);
    b[0] = 1; b[1] = 1; b[2] = 6; put32(b + 4, xid); put16(b + 10, 0x8000);          /* answer by broadcast */
    if (type == 3 && net.phase == NET_READY && !net.link_local) put32(b + 12, net.ip);  /* renewing */
    memcpy(b + 28, net.mac, 6);
    put32(b + 236, 0x63825363);
    int o = 240;
    b[o++] = 53; b[o++] = 1; b[o++] = (uint8_t)type;
    if (type == 3 && offered && !(net.phase == NET_READY && !net.link_local)) { b[o++] = 50; b[o++] = 4; put32(b + o, offered); o += 4; b[o++] = 54; b[o++] = 4; put32(b + o, net.dhcp_server); o += 4; }
    b[o++] = 55; b[o++] = 3; b[o++] = 1; b[o++] = 3; b[o++] = 51;
    b[o++] = 12; b[o++] = 4; memcpy(b + o, "bare", 4); o += 4;
    b[o++] = 255;
    int n = udp_build(0, 0xFFFFFFFFu, 68, 67, b, MAX(o, 300));
    ip_send(type == 3 && net.phase == NET_READY && !net.link_local ? net.ip : 0, 0xFFFFFFFFu, P_UDP, n, 64);
}
static void dhcp_start(uint64_t now) {
    xid = (uint32_t)now * 2654435761u ^ be32(net.mac + 2); offered = 0; dhcp_tries = 0; dhcp_at = now;
    if (net.phase != NET_READY) { net.phase = NET_DHCP; phase_at = now; snfmt(net.status, sizeof net.status, "asking for an address (DHCP)"); }
}
static void dhcp_in(const uint8_t *b, int len, uint64_t now) {
    if (len < 240 || b[0] != 2 || be32(b + 4) != xid || be32(b + 236) != 0x63825363 || memcmp(b + 28, net.mac, 6)) return;
    int type = 0; uint32_t mask = 0, router = 0, server = 0, lease = 3600;
    for (int o = 240; o + 1 < len && b[o] != 255;) {
        if (!b[o]) { o++; continue; }
        int t = b[o], l = b[o + 1]; const uint8_t *v = b + o + 2;
        if (o + 2 + l > len) break;
        if (t == 53 && l >= 1) type = v[0];
        else if (t == 1 && l >= 4) mask = be32(v);
        else if (t == 3 && l >= 4) router = be32(v);
        else if (t == 54 && l >= 4) server = be32(v);
        else if (t == 51 && l >= 4) lease = be32(v);
        o += 2 + l;
    }
    if (type == 2 && !offered) {                                       /* OFFER: take it */
        offered = be32(b + 16); net.dhcp_server = server;
        dhcp_send(3);
    } else if (type == 5 && (be32(b + 16) == offered || (net.phase == NET_READY && be32(b + 16) == net.ip))) {   /* ACK */
        bool was_ll = net.link_local;
        net.ip = be32(b + 16); net.mask = mask ? mask : 0xFFFFFF00u; net.gw = router; net.lease_s = MAX(lease, 60u);
        lease_until = (uint32_t)(now / 1000) + net.lease_s; net.link_local = false;
        if (net.phase != NET_READY || was_ll) {
            char a[20], g[20]; net_ip_str(net.ip, a, sizeof a); net_ip_str(net.gw, g, sizeof g);
            logf("net: %s from DHCP (router %s, %u s)", a, g, net.lease_s);
            snfmt(net.status, sizeof net.status, "%s (DHCP)", a);
            net.phase = NET_READY;
            for (int i = 0; i < ngroups; i++) igmp_report(groups[i]);
            arp_send(1, (const uint8_t[6]){ 0 }, net.ip, net.ip, bcast_mac);   /* announce it */
        }
        offered = 0;
    } else if (type == 6) { offered = 0; dhcp_tries = 0; }              /* NAK: again */
}
static uint32_t ll_address(void) {                                     /* 169.254.1.0 to 169.254.254.255, from the MAC */
    uint32_t h = be32(net.mac + 2) * 2654435761u + ll_try * 40503u;
    return NET_IP(169, 254, 1 + (h >> 8) % 254, h & 255);
}

/* ---- in ---- */
static bool ours(uint32_t dst) {
    if (dst == net.ip && net.phase == NET_READY) return true;
    if (dst == 0xFFFFFFFFu || (net.mask && (dst | net.mask) == 0xFFFFFFFFu)) return true;
    for (int i = 0; i < ngroups; i++) if (groups[i] == dst) return true;
    return false;
}
static void ip_in(const uint8_t *h, int len, uint64_t now) {
    if (len < 20 || (h[0] >> 4) != 4) return;
    int hl = (h[0] & 15) * 4, tot = be16(h + 2);
    if (hl < 20 || tot > len || tot < hl || fold(sum16(h, hl, 0)) != 0) { net.dropped++; return; }
    if (be16(h + 6) & 0x3FFF) { net.dropped++; return; }               /* a fragment */
    uint32_t src = be32(h + 12), dst = be32(h + 16);
    const uint8_t *p = h + hl; int n = tot - hl;
    if (h[9] == P_UDP && n >= 8) {
        uint16_t sport = be16(p), dport = be16(p + 2), ul = be16(p + 4);
        if (ul < 8 || ul > n) return;
        if (dport == 68) { dhcp_in(p + 8, ul - 8, now); return; }
        if (!ours(dst)) return;
        for (int i = 0; i < nports; i++) if (ports[i].port == dport) ports[i].h(src, sport, dst, p + 8, ul - 8, now);
        return;
    }
    if (!ours(dst)) return;
    if (h[9] == P_ICMP && n >= 8 && p[0] == 8 && dst == net.ip) {       /* ping */
        uint8_t *r = tx + 14 + 20;
        int m = MIN(n, 1472);
        memcpy(r, p, (size_t)m); r[0] = 0; put16(r + 2, 0); put16(r + 2, fold(sum16(r, m, 0)));
        ip_send(net.ip, src, P_ICMP, m, 64);
    } else if (h[9] == P_IGMP && n >= 8 && p[0] == 0x11)                /* a router asks who's in which group */
        igmp_at = now > 2000 ? now - 58000 : 0;                          /* answer within two seconds */
}
static void arp_in(const uint8_t *a, int len) {
    if (len < 28 || be16(a) != 1 || be16(a + 2) != ETH_IP || a[4] != 6 || a[5] != 4) return;
    uint16_t op = be16(a + 6); uint32_t spa = be32(a + 14), tpa = be32(a + 24);
    if (net.phase == NET_PROBING && (spa == ll_address() || (!spa && tpa == ll_address() && memcmp(a + 8, net.mac, 6)))) {
        char s[20]; net_ip_str(ll_address(), s, sizeof s);
        logf("net: %s is taken, another one", s);
        ll_try++; probes = 0; phase_at = now_ms;
        return;
    }
    if (net.phase == NET_READY && spa == net.ip && memcmp(a + 8, net.mac, 6)) logf("net: another machine uses our address");
    arp_learn(spa, a + 8);
    if (op == 1 && net.phase == NET_READY && tpa == net.ip) arp_send(2, a + 8, net.ip, spa, a + 8);
}

void net_init(void) {
    memset(&net, 0, sizeof net);
    memset(arp, 0, sizeof arp); memset(pend, 0, sizeof pend);                    /* what was learnt; the listeners and groups stay */
    link = false; link_at = 0; offered = 0; lease_until = 0; ll_try = 0; probes = 0; igmp_at = 0;
    if (!plat_net_present(net.mac)) { net.phase = NET_NONE; snfmt(net.status, sizeof net.status, "no network port"); return; }
    net.phase = NET_NO_LINK;
    snfmt(net.status, sizeof net.status, "no cable");
    logf("net: port %02x:%02x:%02x:%02x:%02x:%02x", net.mac[0], net.mac[1], net.mac[2], net.mac[3], net.mac[4], net.mac[5]);
}

void net_work(uint64_t now) {
    now_ms = now;
    if (net.phase == NET_NONE) {                                       /* no port (yet): a USB adapter may come */
        if (now - look_at < 1000) return;
        look_at = now;
        if (!plat_net_present(net.mac)) return;
        net_init();
    } else if (now - look_at >= 1000) {
        look_at = now;
        uint8_t m[6];
        if (!plat_net_present(m)) { logf("net: the port went away"); memset(&net, 0, sizeof net); net.phase = NET_NONE; snfmt(net.status, sizeof net.status, "no network port"); return; }
    }
    if (now - link_at >= 250 || !link_at) {                            /* the cable */
        link_at = now;
        bool l = plat_net_link();
        if (l && !link) { logf("net: link up"); ll_try = 0; dhcp_start(now); }
        if (!l && link) { logf("net: link down"); net.phase = NET_NO_LINK; net.ip = 0; snfmt(net.status, sizeof net.status, "no cable"); }
        link = l;
    }
    for (int i = 0; i < 32; i++) {                                      /* frames in */
        int n = plat_net_recv(rx, sizeof rx);
        if (n <= 0) break;
        net.rx++;
        if (n < 14) continue;
        uint16_t type = be16(rx + 12);
        if (type == ETH_ARP) arp_in(rx + 14, n - 14);
        else if (type == ETH_IP) ip_in(rx + 14, n - 14, now);
    }
    if (!link) return;
    /* DHCP: three tries over six seconds, then an address of our own; while on that, a try every half minute */
    if (net.phase == NET_DHCP || (net.phase == NET_READY && net.link_local)) {
        uint64_t wait = net.phase == NET_READY ? 30000 : dhcp_tries == 0 ? 0 : dhcp_tries == 1 ? 1500 : 2000;
        if (now - dhcp_at >= wait && (net.phase == NET_READY || dhcp_tries < 3)) { offered = 0; dhcp_send(1); dhcp_tries++; dhcp_at = now; }
        if (net.phase == NET_DHCP && now - phase_at > 6000) { net.phase = NET_PROBING; probes = 0; phase_at = now - 1000; snfmt(net.status, sizeof net.status, "no DHCP: picking an address"); }
    }
    if (net.phase == NET_READY && !net.link_local && lease_until && now / 1000 >= lease_until - net.lease_s / 2 && now - dhcp_at > 10000) {
        dhcp_send(3); dhcp_at = now;                                    /* renew at half the lease */
        if (now / 1000 >= lease_until) { net.phase = NET_DHCP; net.ip = 0; dhcp_start(now); }
    }
    /* link-local: three ARP probes a second apart, then two announcements */
    if (net.phase == NET_PROBING && now - phase_at >= 1000) {
        phase_at = now;
        uint32_t a = ll_address();
        if (probes < 3) { arp_send(1, (const uint8_t[6]){ 0 }, 0, a, bcast_mac); probes++; }
        else {
            net.ip = a; net.mask = 0xFFFF0000u; net.gw = 0; net.link_local = true; net.phase = NET_READY;
            arp_send(1, (const uint8_t[6]){ 0 }, a, a, bcast_mac); arp_send(1, (const uint8_t[6]){ 0 }, a, a, bcast_mac);
            char s[20]; net_ip_str(a, s, sizeof s);
            snfmt(net.status, sizeof net.status, "%s (link-local)", s);
            logf("net: %s, link-local (no DHCP server answered)", s);
            for (int i = 0; i < ngroups; i++) igmp_report(groups[i]);
            dhcp_at = now;
        }
    }
    for (int p = 0; p < PENDING; p++) {                                 /* ARP: ask again, give up after three */
        if (!pend[p].len || now - pend[p].asked < 1000) continue;
        if (pend[p].tries >= 3) { pend[p].len = 0; net.dropped++; continue; }
        pend[p].tries++; pend[p].asked = now;
        arp_send(1, (const uint8_t[6]){ 0 }, net.ip, pend[p].ip, bcast_mac);
    }
    if (net.phase == NET_READY && ngroups && now - igmp_at >= 60000) { igmp_at = now; for (int i = 0; i < ngroups; i++) igmp_report(groups[i]); }
}
