/* Intel High Definition Audio: CORB/RIRB verbs, an output stream (one BDL ring, refilled from the timer) and an input
   stream for the line in / microphones (read from the timer into the core's input FIFO). */
#include "hda.h"
#include "sound.h"
#include "libc.h"
#include "io.h"
#include "pci.h"
#include "mem.h"
#include "pit.h"
#include "audio.h"
#include "log.h"
#include "platform.h"

#define GCAP 0x00
#define GCTL 0x08
#define WAKEEN 0x0C
#define STATESTS 0x0E
#define INTCTL 0x20
#define CORBLBASE 0x40
#define CORBUBASE 0x44
#define CORBWP 0x48
#define CORBRP 0x4A
#define CORBCTL 0x4C
#define CORBSIZE 0x4E
#define RIRBLBASE 0x50
#define RIRBUBASE 0x54
#define RIRBWP 0x58
#define RINTCNT 0x5A
#define RIRBCTL 0x5C
#define RIRBSTS 0x5D
#define RIRBSIZE 0x5E
#define DPLBASE 0x70
#define DPUBASE 0x74
#define SD_BASE 0x80
#define SD_CTL0 0x00
#define SD_CTL2 0x02
#define SD_STS 0x03
#define SD_LPIB 0x04
#define SD_CBL 0x08
#define SD_LVI 0x0C
#define SD_FMT 0x12
#define SD_BDPL 0x18
#define SD_BDPU 0x1C

#define RING_FRAMES 4096            /* 85 ms at 48 kHz, power of two */
#define BDL_ENTRIES 4
#define LEAD_FRAMES 512             /* how far ahead of the DMA head we keep the ring filled (~11 ms; the pump runs every 1 ms) */
#define CHUNK_FRAMES 128
#define RATE 48000

static volatile uint8_t *regs;
static volatile uint32_t *corb, *rirb;
static uint32_t corb_n, rirb_n, corb_wp, rirb_rp;
static volatile uint8_t *sd;
static int16_t *ring;
static uint32_t wr_frames;
static bool ready;
static int codec_addr;
struct outpin { uint8_t nid, dev; bool sense, trigger, has_amp, eapd; };
static struct outpin outpins[8]; static int noutpins;
static bool hp_present, all_on, jack_dirty;
static uint16_t ctl_vendor, ctl_device; static uint32_t codec_vendor, pumps;
static bool tone; static uint32_t tone_phase;
static uint32_t lead = LEAD_FRAMES;
/* inputs: a pin that can take sound in, and the converter (ADC) that reaches it */
struct inpin { uint8_t nid, dev, conn, adc; bool sense; char name[16]; };
static struct inpin inpins[6]; static int ninpins, in_active = -1;
static volatile uint8_t *isd;                     /* the input stream descriptor, 0 if the controller has none */
static int16_t *in_ring; static uint32_t in_rd; static bool in_running;

static inline uint32_t r32(uint32_t o) { return mmio_r32(regs + o); }
static inline uint16_t r16(uint32_t o) { return mmio_r16(regs + o); }
static inline uint8_t  r8(uint32_t o)  { return mmio_r8(regs + o); }
static inline void w32(uint32_t o, uint32_t v) { mmio_w32(regs + o, v); }
static inline void w16(uint32_t o, uint16_t v) { mmio_w16(regs + o, v); }
static inline void w8(uint32_t o, uint8_t v)   { mmio_w8(regs + o, v); }

static bool wait_bits8(uint32_t off, uint8_t mask, uint8_t want, uint32_t ms) {
    uint64_t end = pit_ticks() + ms;
    while (pit_ticks() < end) { if ((r8(off) & mask) == want) return true; }
    return (r8(off) & mask) == want;
}
static bool wait_bits32(uint32_t off, uint32_t mask, uint32_t want, uint32_t ms) {
    uint64_t end = pit_ticks() + ms;
    while (pit_ticks() < end) { if ((r32(off) & mask) == want) return true; }
    return (r32(off) & mask) == want;
}

/* ---- verbs ---- */
static bool verb(uint32_t cmd, uint32_t *resp) {
    corb_wp = (corb_wp + 1) % corb_n;
    corb[corb_wp] = cmd;
    barrier();
    w16(CORBWP, (uint16_t)corb_wp);
    uint64_t end = pit_ticks() + 50;
    for (;;) {
        uint16_t wp = r16(RIRBWP) & 0xFF;
        while (rirb_rp != wp) {
            rirb_rp = (rirb_rp + 1) % rirb_n;
            uint32_t r = rirb[rirb_rp * 2], ex = rirb[rirb_rp * 2 + 1];
            w8(RIRBSTS, 0x5);                        /* ack: resets the response-interrupt counter */
            if (ex & 0x10) continue;                 /* unsolicited */
            if (resp) *resp = r;
            return true;
        }
        if (pit_ticks() > end) { logf("hda: verb %08x timed out", cmd); return false; }
    }
}
static uint32_t cmd12(int nid, uint32_t v, uint32_t payload) {
    uint32_t c = ((uint32_t)codec_addr << 28) | ((uint32_t)nid << 20) | (v << 8) | (payload & 0xFF);
    uint32_t r = 0; verb(c, &r); return r;
}
static uint32_t cmd4(int nid, uint32_t v, uint32_t payload) {
    uint32_t c = ((uint32_t)codec_addr << 28) | ((uint32_t)nid << 20) | (v << 16) | (payload & 0xFFFF);
    uint32_t r = 0; verb(c, &r); return r;
}
#define PARAM(nid, p)         cmd12(nid, 0xF00, p)
#define P_SUBNODES 0x04
#define P_FG_TYPE 0x05
#define P_WCAPS 0x09
#define P_PINCAPS 0x0C
#define P_INAMP 0x0D
#define P_CONNLEN 0x0E
#define P_OUTAMP 0x12
#define W_OUTPUT 0
#define W_INPUT 1
#define W_MIXER 2
#define W_SELECTOR 3
#define W_PIN 4

static void set_amp(int nid, bool output, int index, uint32_t gain) {
    uint32_t p = (output ? 0x8000 : 0x4000) | 0x3000 | ((uint32_t)index << 8) | (gain & 0x7F);
    cmd4(nid, 0x3, p);
}
static uint32_t amp_offset(int nid, bool output) {
    uint32_t caps = PARAM(nid, output ? P_OUTAMP : P_INAMP);
    return caps & 0x7F;                                  /* 0 dB step */
}

/* Connection list of nid into out[]; returns count. Expands ranges. */
static int conn_list(int nid, uint16_t *out, int max) {
    uint32_t len = PARAM(nid, P_CONNLEN);
    bool longform = len & 0x80; int n = len & 0x7F, count = 0;
    int per = longform ? 2 : 4, bits = longform ? 16 : 8;
    uint32_t mask = longform ? 0xFFFF : 0xFF, rangebit = longform ? 0x8000 : 0x80;
    for (int i = 0; i < n; i += per) {
        uint32_t r = cmd12(nid, 0xF02, i);
        for (int k = 0; k < per && i + k < n; k++) {
            uint32_t e = (r >> (k * bits)) & mask;
            if (e & rangebit) {
                uint16_t prev = count ? out[count - 1] : 0;
                for (uint16_t x = prev + 1; x <= (e & ~rangebit) && count < max; x++) out[count++] = x;
            } else if (count < max) out[count++] = (uint16_t)e;
        }
    }
    return count;
}

static int route_to_dac(int nid, int depth) {
    uint32_t caps = PARAM(nid, P_WCAPS);
    int type = (caps >> 20) & 0xF;
    if (type == W_OUTPUT) return nid;
    if (depth > 5 || !(caps & 0x100)) return 0;
    uint16_t conns[32]; int n = conn_list(nid, conns, 32);
    for (int i = 0; i < n; i++) {
        int dac = route_to_dac(conns[i], depth + 1);
        if (!dac) continue;
        cmd12(nid, 0x705, 0);                                     /* D0 */
        if (type == W_PIN || type == W_SELECTOR) cmd12(nid, 0x701, i);
        if (caps & 0x2) set_amp(nid, false, i, amp_offset(nid, false));    /* input amp for this index */
        if (caps & 0x4) set_amp(nid, true, 0, amp_offset(nid, true));      /* output amp */
        return dac;
    }
    return 0;
}

/* A path from a converter down to a pin, through mixers and selectors: the widgets and the connection taken at each. */
static uint8_t pnid[6], pidx[6]; static int plen;
static bool path_to(int nid, int pin, int depth) {
    if (depth >= 6 || !(PARAM(nid, P_WCAPS) & 0x100)) return false;
    uint16_t conns[32]; int n = conn_list(nid, conns, 32);
    for (int i = 0; i < n; i++) {
        pnid[depth] = (uint8_t)nid; pidx[depth] = (uint8_t)i;
        if (conns[i] == pin) { plen = depth + 1; return true; }
        int t = (PARAM(conns[i], P_WCAPS) >> 20) & 0xF;
        if ((t == W_MIXER || t == W_SELECTOR) && path_to(conns[i], pin, depth + 1)) return true;
    }
    return false;
}

/* Turn one input on: the path from its converter, the pin (input enabled, a bias voltage for a microphone in a jack,
   +20 dB of boost for a microphone where the pin has it), the converter at 48 kHz stereo on stream 2. */
static bool configure_input(const struct inpin *p) {
    if (!path_to(p->adc, p->nid, 0)) return false;
    for (int k = 0; k < plen; k++) {
        int nid = pnid[k], idx = pidx[k];
        uint32_t caps = PARAM(nid, P_WCAPS);
        uint16_t conns[32]; int n = conn_list(nid, conns, 32);
        cmd12(nid, 0x705, 0);
        if (((caps >> 20) & 0xF) == W_MIXER) {                     /* a mixer: this input on, the others muted */
            if (caps & 0x2) for (int j = 0; j < n; j++) { if (j == idx) set_amp(nid, false, j, amp_offset(nid, false)); else cmd4(nid, 0x3, 0x7080 | (uint32_t)j << 8); }
        } else {
            if (n > 1) cmd12(nid, 0x701, (uint32_t)idx);
            if (caps & 0x2) set_amp(nid, false, idx, amp_offset(nid, false));
        }
        if (caps & 0x4) set_amp(nid, true, 0, amp_offset(nid, true));
    }
    uint32_t pc = PARAM(p->nid, P_PINCAPS), wc = PARAM(p->nid, P_WCAPS);
    bool mic = p->dev == 0xA;
    uint32_t vref = mic && p->conn != 2 ? ((pc & (1u << 12)) ? 4 : (pc & (1u << 9)) ? 1 : 0) : 0;
    cmd12(p->nid, 0x705, 0);
    cmd12(p->nid, 0x707, 0x20 | vref);
    if (wc & 0x2) {
        uint32_t ac = PARAM(p->nid, P_INAMP), steps = (ac >> 8) & 0x7F, off = ac & 0x7F;
        set_amp(p->nid, false, 0, mic ? MIN(steps, off + 2) : off);
    }
    cmd12(p->adc, 0x705, 0);
    cmd4(p->adc, 0x2, 0x0011);                                    /* 48 kHz 16-bit stereo */
    cmd12(p->adc, 0x706, 0x20);                                   /* stream 2, channel 0 */
    logf("hda: input %s: pin %02x via %d widgets to adc %02x%s", p->name, p->nid, plen, p->adc, vref ? ", mic bias" : "");
    return true;
}

/* input pins with a converter that reaches them; a jack's microphone, the built-in one, line in */
static void find_inputs(int w0, int wn) {
    uint8_t adcs[8]; int nadc = 0;
    for (int w = w0; w < w0 + wn && nadc < 8; w++) if (((PARAM(w, P_WCAPS) >> 20) & 0xF) == W_INPUT) adcs[nadc++] = (uint8_t)w;
    for (int w = w0; w < w0 + wn && ninpins < 6; w++) {
        if (((PARAM(w, P_WCAPS) >> 20) & 0xF) != W_PIN) continue;
        uint32_t pc = PARAM(w, P_PINCAPS), cfg = cmd12(w, 0xF1C, 0);
        int conn = cfg >> 30, dev = (cfg >> 20) & 0xF;
        if (!(pc & 0x20) || conn == 1 || (dev != 0x8 && dev != 0xA && dev != 0x9)) continue;
        int adc = -1;
        for (int a = 0; a < nadc && adc < 0; a++) if (path_to(adcs[a], w, 0)) adc = adcs[a];
        if (adc < 0) continue;
        struct inpin *p = &inpins[ninpins];
        *p = (struct inpin){ (uint8_t)w, (uint8_t)dev, (uint8_t)conn, (uint8_t)adc, (pc & 0x4) != 0, "" };
        const char *base = dev == 0xA ? (conn == 2 ? "BUILT-IN MIC" : "MIC") : dev == 0x8 ? "LINE IN" : "AUX IN";
        int same = 0; for (int i = 0; i < ninpins; i++) if (inpins[i].dev == dev && (inpins[i].conn == 2) == (conn == 2)) same++;
        if (same) snfmt(p->name, sizeof p->name, "%s %d", base, same + 1); else snfmt(p->name, sizeof p->name, "%s", base);
        logf("hda:  input pin %02x (%s) <- adc %02x, config %08x, caps %08x", w, p->name, adc, cfg, pc);
        ninpins++;
    }
}

static bool setup_codec(void) {
    uint32_t sub = PARAM(0, P_SUBNODES);
    int fg_start = (sub >> 16) & 0xFF, fg_count = sub & 0xFF, outputs = 0;
    codec_vendor = PARAM(0, 0);
    logf("hda: codec %d vendor %08x, %d function groups", codec_addr, codec_vendor, fg_count);
    for (int fg = fg_start; fg < fg_start + fg_count; fg++) {
        if ((PARAM(fg, P_FG_TYPE) & 0x7F) != 1) continue;
        cmd12(fg, 0x705, 0);
        uint32_t ws = PARAM(fg, P_SUBNODES);
        int w0 = (ws >> 16) & 0xFF, wn = ws & 0xFF;
        for (int w = w0; w < w0 + wn; w++) {
            uint32_t caps = PARAM(w, P_WCAPS);
            if (((caps >> 20) & 0xF) != W_PIN) continue;
            uint32_t pincaps = PARAM(w, P_PINCAPS), cfg = cmd12(w, 0xF1C, 0);
            int conn = cfg >> 30, dev = (cfg >> 20) & 0xF;
            if (!(pincaps & 0x10) || conn == 1) continue;             /* not output-capable / not connected */
            if (dev > 2) continue;                                      /* 0 line out, 1 speaker, 2 headphone */
            int dac = route_to_dac(w, 0);
            if (!dac) continue;
            cmd12(dac, 0x705, 0);
            cmd4(dac, 0x2, 0x0011);                                     /* 48 kHz 16-bit stereo */
            cmd12(dac, 0x706, 0x10);                                    /* stream 1, channel 0 */
            if (PARAM(dac, P_WCAPS) & 0x4) set_amp(dac, true, 0, amp_offset(dac, true));
            cmd12(w, 0x707, 0x40 | (dev == 2 ? 0x80 : 0));              /* OUT enable (+HP) */
            if (pincaps & 0x10000) cmd12(w, 0x70C, 0x02);               /* EAPD */
            if (noutpins < 8) outpins[noutpins++] = (struct outpin){ (uint8_t)w, (uint8_t)dev, (pincaps & 0x4) != 0, (pincaps & 0x8) != 0,
                                                                  (PARAM(w, P_WCAPS) & 0x4) != 0, (pincaps & 0x10000) != 0 };
            logf("hda:  pin %02x (%s) -> dac %02x, config %08x, caps %08x", w, dev == 0 ? "line out" : dev == 1 ? "speaker" : "headphone", dac, cfg, pincaps);
            outputs++;
        }
        find_inputs(w0, wn);
    }
    return outputs > 0;
}

/* the first input stream descriptor, set up and left stopped until an input is chosen */
static void stream_reset(volatile uint8_t *d) {
    uint32_t off = (uint32_t)(d - regs);
    mmio_w8(d + SD_CTL0, 0); wait_bits8(off + SD_CTL0, 2, 0, 20);
    mmio_w8(d + SD_CTL0, 1); wait_bits8(off + SD_CTL0, 1, 1, 20);
    mmio_w8(d + SD_CTL0, 0); wait_bits8(off + SD_CTL0, 1, 0, 20);
}
static uint64_t in_bdl_phys;
static void input_stream_program(void) {                        /* after a reset: the registers start from zero */
    stream_reset(isd);
    mmio_w32(isd + SD_CBL, RING_FRAMES * 4);
    mmio_w16(isd + SD_LVI, BDL_ENTRIES - 1);
    mmio_w16(isd + SD_FMT, 0x0011);
    mmio_w32(isd + SD_BDPL, (uint32_t)in_bdl_phys);
    mmio_w32(isd + SD_BDPU, (uint32_t)(in_bdl_phys >> 32));
    mmio_w8(isd + SD_CTL2, 0x20);                               /* stream number 2 */
    mmio_w8(isd + SD_STS, 0x1C);
}
static void setup_input_stream(void) {
    isd = regs + SD_BASE;
    uint64_t ring_phys;
    in_ring = dma_alloc(RING_FRAMES * 4, &ring_phys);
    uint64_t *bdl = dma_alloc(4096, &in_bdl_phys);
    uint32_t per = RING_FRAMES * 4 / BDL_ENTRIES;
    for (int i = 0; i < BDL_ENTRIES; i++) { bdl[i * 2] = ring_phys + (uint64_t)i * per; bdl[i * 2 + 1] = per; }
    input_stream_program();
    logf("hda: %d input(s), input stream ready", ninpins);
}

int hda_inputs(const char **names, int max) {
    int n = isd ? MIN(ninpins, max) : 0;
    for (int i = 0; i < n; i++) names[i] = inpins[i].name;
    return n;
}
int hda_input_active(void) { return in_active; }
bool hda_input_jack(int i) {
    if (i < 0 || i >= ninpins || !inpins[i].sense) return false;
    return (cmd12(inpins[i].nid, 0xF09, 0) & 0x80000000u) != 0;
}
bool hda_input_select(int i) {
    if (i >= ninpins || !isd) return i < 0;
    if (in_running) { mmio_w8(isd + SD_CTL0, 0); in_running = false; }
    if (in_active >= 0) {                                       /* the old pin stops listening, unless it also plays */
        bool out = false; for (int k = 0; k < noutpins; k++) if (outpins[k].nid == inpins[in_active].nid) out = true;
        if (!out) cmd12(inpins[in_active].nid, 0x707, 0);
        in_active = -1;
    }
    if (i < 0) { logf("hda: input off"); return true; }
    if (!configure_input(&inpins[i])) return false;
    input_stream_program();
    in_rd = 0;
    mmio_w8(isd + SD_CTL0, 0x2);                                /* RUN */
    in_running = true; in_active = i;
    return true;
}

static bool hda_try(const struct pci_dev *d);
/* Intel's audio DSPs (Skylake on; class 04:01 where the laptop has digital microphones) keep an HDA controller for the
   analog codec — headphones and speakers — at BAR 0. Linux's HDA driver takes these IDs too. */
static bool intel_dsp(uint16_t id) {
    static const uint16_t ids[] = { 0x9D70, 0x9D71, 0xA170, 0xA171, 0xA2F0, 0x5A98, 0x3198, 0x9DC8, 0xA348, 0x02C8, 0x06C8,
                                    0xA3F0, 0x34C8, 0x3DC8, 0x38C8, 0x4DC8, 0xA0C8, 0x43C8, 0x4B55, 0x4B58, 0x51C8, 0x51C9,
                                    0x51CA, 0x51CB, 0x51CC, 0x51CD, 0x51CE, 0x51CF, 0x54C8, 0x7AD0, 0x7A50, 0x7E28, 0x7F50, 0xA828 };
    for (unsigned i = 0; i < ARRAY_LEN(ids); i++) if (ids[i] == id) return true;
    return false;
}

bool hda_init(void) {
    /* A machine can have several HDA controllers (GPU HDMI audio is one). Try the chipset one first, then any that
       has an analog output pin. */
    struct pci_dev devs[6]; int n = pci_find_all_class(0x04, 0x03, devs, 4);
    struct pci_dev dsp[2]; int m = pci_find_all_class(0x04, 0x01, dsp, 2);
    for (int i = 0; i < m && n < 6; i++) if (dsp[i].vendor == 0x8086 && intel_dsp(dsp[i].device)) devs[n++] = dsp[i];
    if (!n) { logf("hda: no controller found"); return false; }
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            bool chipset = devs[i].bus == 0 && (devs[i].vendor == 0x8086 || devs[i].vendor == 0x1022 || (devs[i].vendor == 0x1002 && devs[i].dev >= 0x14));
            if ((pass == 0) != chipset) continue;
            if (hda_try(&devs[i])) return true;
        }
    return false;
}

static bool hda_try(const struct pci_dev *dp) {
    struct pci_dev d = *dp;
    bool is_mem; uint64_t bar = pci_bar(&d, 0, &is_mem);
    logf("hda: %04x:%04x at %02x:%02x.%d bar0 %lx", d.vendor, d.device, d.bus, d.dev, d.fn, bar);
    ctl_vendor = d.vendor; ctl_device = d.device;
    if (!is_mem || !bar) return false;
    if ((uint64_t)(uintptr_t)bar != bar) { logf("hda: bar above 4 GiB, unreachable on this build"); return false; }
    pci_enable(&d);
    if (d.vendor == 0x8086) {
        /* as Linux does for Intel HDA: traffic class 0 (TCSEL, 0x44), and snooped DMA (DEVC 0x78 bit 11 clear) so the
           controller sees the ring through the CPU cache; firmware can leave no-snoop on */
        uint32_t tc = pci_read32(d.bus, d.dev, d.fn, 0x44), devc = pci_read32(d.bus, d.dev, d.fn, 0x78);
        if (tc & 7) { pci_write32(d.bus, d.dev, d.fn, 0x44, tc & ~7u); logf("hda: TCSEL was %u, now 0", tc & 7); }
        if (devc & 0x800) { pci_write32(d.bus, d.dev, d.fn, 0x78, devc & ~0x800u); logf("hda: no-snoop DMA was on, now off"); }
    }
    regs = mmio_map(bar, 0x4000);
    ready = false; ring = 0;

    /* reset */
    w32(GCTL, r32(GCTL) & ~1u);
    if (!wait_bits32(GCTL, 1, 0, 100)) { logf("hda: reset entry timeout"); return false; }
    sleep_ms(2);
    w32(GCTL, r32(GCTL) | 1);
    if (!wait_bits32(GCTL, 1, 1, 100)) { logf("hda: reset exit timeout"); return false; }
    sleep_ms(5);
    uint16_t gcap = r16(GCAP), statests = r16(STATESTS);
    int iss = (gcap >> 8) & 0xF, oss = (gcap >> 12) & 0xF;
    logf("hda: gcap %04x (in %d out %d) codecs %04x", gcap, iss, oss, statests);
    if (!statests || !oss) return false;
    codec_addr = 0; while (!(statests & (1 << codec_addr))) codec_addr++;

    /* CORB / RIRB */
    w8(CORBCTL, 0); w8(RIRBCTL, 0);
    wait_bits8(CORBCTL, 2, 0, 50); wait_bits8(RIRBCTL, 2, 0, 50);
    uint64_t corb_phys, rirb_phys;
    corb = dma_alloc(4096, &corb_phys); rirb = dma_alloc(4096, &rirb_phys);
    uint8_t csz = r8(CORBSIZE), rsz = r8(RIRBSIZE);
    if (csz & 0x40) { corb_n = 256; w8(CORBSIZE, (csz & ~3) | 2); } else if (csz & 0x20) { corb_n = 16; w8(CORBSIZE, (csz & ~3) | 1); } else corb_n = 2;
    if (rsz & 0x40) { rirb_n = 256; w8(RIRBSIZE, (rsz & ~3) | 2); } else if (rsz & 0x20) { rirb_n = 16; w8(RIRBSIZE, (rsz & ~3) | 1); } else rirb_n = 2;
    w32(CORBLBASE, (uint32_t)corb_phys); w32(CORBUBASE, (uint32_t)(corb_phys >> 32));
    w32(RIRBLBASE, (uint32_t)rirb_phys); w32(RIRBUBASE, (uint32_t)(rirb_phys >> 32));
    w16(CORBRP, 0x8000); sleep_ms(1); w16(CORBRP, 0); sleep_ms(1);
    w16(CORBWP, 0); corb_wp = 0;
    w16(RIRBWP, 0x8000); rirb_rp = 0;
    w16(RINTCNT, 1);
    w8(RIRBSTS, 0x5);
    w8(RIRBCTL, 0x3); w8(CORBCTL, 0x2);                     /* RIRB DMA + response interrupt flag (no IRQ: INTCTL.GIE stays 0) */
    wait_bits8(CORBCTL, 2, 2, 50);
    logf("hda: corb %u entries, rirb %u entries", corb_n, rirb_n);

    noutpins = 0; hp_present = false; ninpins = 0; in_active = -1; in_running = false; isd = 0;
    if (!setup_codec()) { logf("hda: no usable output pin"); return false; }

    /* output stream: first output descriptor comes after the input ones */
    sd = regs + SD_BASE + (uint32_t)iss * 0x20;
    mmio_w8(sd + SD_CTL0, 0);
    wait_bits8(SD_BASE + iss * 0x20 + SD_CTL0, 2, 0, 20);
    mmio_w8(sd + SD_CTL0, 1);                                   /* SRST */
    wait_bits8(SD_BASE + iss * 0x20 + SD_CTL0, 1, 1, 20);
    mmio_w8(sd + SD_CTL0, 0);
    wait_bits8(SD_BASE + iss * 0x20 + SD_CTL0, 1, 0, 20);

    uint64_t ring_phys, bdl_phys;
    ring = dma_alloc(RING_FRAMES * 4, &ring_phys);
    uint64_t *bdl = dma_alloc(4096, &bdl_phys);
    uint32_t per = RING_FRAMES * 4 / BDL_ENTRIES;
    for (int i = 0; i < BDL_ENTRIES; i++) { bdl[i * 2] = ring_phys + (uint64_t)i * per; bdl[i * 2 + 1] = per; }
    mmio_w32(sd + SD_CBL, RING_FRAMES * 4);
    mmio_w16(sd + SD_LVI, BDL_ENTRIES - 1);
    mmio_w16(sd + SD_FMT, 0x0011);
    mmio_w32(sd + SD_BDPL, (uint32_t)bdl_phys);
    mmio_w32(sd + SD_BDPU, (uint32_t)(bdl_phys >> 32));
    mmio_w8(sd + SD_CTL2, 0x10);                                /* stream number 1 */
    mmio_w8(sd + SD_STS, 0x1C);
    wr_frames = 0;
    hda_pump(true);                                             /* prime with silence */
    mmio_w8(sd + SD_CTL0, 0x2);                                 /* RUN */
    ready = true;
    logf("hda: stream running, ring %d frames, lead %d", RING_FRAMES, LEAD_FRAMES);
    if (iss && ninpins) setup_input_stream();
    return true;
}

uint32_t hda_rate(void) { return RATE; }
uint32_t hda_latency(void) { return lead + CHUNK_FRAMES / 2; }
bool hda_headphones(void) { return hp_present; }

static void pin_mute(const struct outpin *o, bool mute) {
    if (o->has_amp) cmd4(o->nid, 0x3, 0xB000 | (mute ? 0x80 : 0) | (mute ? 0 : amp_offset(o->nid, true)));
    cmd12(o->nid, 0x707, mute ? 0x00 : 0x40);
}

/* Every output on at full level with its amplifier powered, whatever the jacks say (a test, from the log view). */
void hda_all_outputs(bool on) {
    all_on = on;
    for (int i = 0; on && i < noutpins; i++) {
        const struct outpin *o = &outpins[i];
        if (o->has_amp) cmd4(o->nid, 0x3, 0xB000 | amp_offset(o->nid, true));
        cmd12(o->nid, 0x707, 0x40 | (o->dev == 2 ? 0x80 : 0));
        if (o->eapd) cmd12(o->nid, 0x70C, 0x02);
    }
    jack_dirty = !on;                                   /* back on the jacks: apply their state at the next poll */
    logf("hda: %s", on ? "every output on, jacks ignored" : "outputs follow the jacks again");
}

void hda_poll_jacks(void) {
    if (!ready || all_on) return;
    bool present = false, any_hp = false;
    for (int i = 0; i < noutpins; i++) {
        struct outpin *o = &outpins[i];
        if (o->dev != 2 || !o->sense) continue;
        any_hp = true;
        if (o->trigger) { cmd12(o->nid, 0x709, 0); }
        uint32_t r = cmd12(o->nid, 0xF09, 0);
        if (r & 0x80000000u) present = true;
    }
    if (!any_hp || (present == hp_present && !jack_dirty)) return;
    hp_present = present; jack_dirty = false;
    for (int i = 0; i < noutpins; i++) if (outpins[i].dev == 1) pin_mute(&outpins[i], present);   /* speakers follow the jack */
    logf("hda: headphones %s", present ? "plugged, speakers muted" : "removed, speakers on");
}

void hda_status(char *out, int cap) {
    if (!ready) { snfmt(out, (size_t)cap, "hda not running"); return; }
    static uint32_t last_pos, last_pumps;
    uint32_t pos = mmio_r32(sd + SD_LPIB), pumped = pumps - last_pumps;
    int n = snfmt(out, (size_t)cap, "%04x:%04x codec %08x · dma %s · pump %s%s", ctl_vendor, ctl_device, codec_vendor,
                  pos != last_pos ? "moving" : "STUCK", pumped ? "running" : "STOPPED", tone ? " · TONE" : "");
    last_pos = pos; last_pumps = pumps;
    for (int i = 0; i < noutpins && n < cap - 40; i++) {           /* per pin: control, EAPD, output amp, jack */
        const struct outpin *o = &outpins[i];
        uint32_t ctl = cmd12(o->nid, 0xF07, 0) & 0xFF, eapd = cmd12(o->nid, 0xF0C, 0) & 0xFF;
        char amp[16] = "none";
        if (o->has_amp) { uint32_t a = cmd4(o->nid, 0xB, 0xA000) & 0xFF; snfmt(amp, sizeof amp, "%s%02x", a & 0x80 ? "MUTE " : "", a & 0x7F); }
        n += snfmt(out + n, (size_t)(cap - n), " · %c%02x ctl %02x eapd %02x amp %s%s", "LSH"[o->dev], o->nid, ctl, eapd, amp,
                   o->sense ? (cmd12(o->nid, 0xF09, 0) & 0x80000000u ? " jack in" : " jack out") : "");
    }
}

void hda_test_tone(bool on) { tone = on; logf("hda: test tone %s", on ? "on" : "off"); }
void hda_long_lead(void) { lead = 2048; logf("hda: %u frames ahead (no timer interrupts)", lead); }

/* the test tone: a 440 Hz square at -12 dB, written over what the engine rendered */
#define TONE_STEP (440u * 65536u / RATE * 65536u)          /* 440 Hz as a step of the 32-bit phase */
static void tone_fill(int16_t *dst, int frames) {
    for (int i = 0; i < frames; i++) {
        tone_phase += TONE_STEP;
        int16_t v = (tone_phase & 0x80000000u) ? 8000 : -8000;
        dst[i * 2] = dst[i * 2 + 1] = v;
    }
}

static bool silent_mode, held;
void hda_hold(bool on) { held = on; }
static void render(int16_t *dst) { if (held) memset(dst, 0, CHUNK_FRAMES * 4); else audio_render(dst, CHUNK_FRAMES); }
void hda_prefill(void) {
    if (!ring || !ready) return;
    uint32_t st = plat_irq_save();
    uint32_t pos = (mmio_r32(sd + SD_LPIB) / 4) & (RING_FRAMES - 1);
    while (((wr_frames - pos) & (RING_FRAMES - 1)) < RING_FRAMES - 2 * CHUNK_FRAMES) {
        int16_t *dst = ring + (size_t)wr_frames * 2;
        render(dst);
        if (silent_mode) memset(dst, 0, CHUNK_FRAMES * 4);
        if (tone) tone_fill(dst, CHUNK_FRAMES);
        wr_frames = (wr_frames + CHUNK_FRAMES) & (RING_FRAMES - 1);
    }
    plat_irq_restore(st);
}

/* what the input stream has written since the last look, to the core */
static void input_drain(void) {
    uint32_t pos = (mmio_r32(isd + SD_LPIB) / 4) & (RING_FRAMES - 1);
    while (in_rd != pos) {
        uint32_t n = pos > in_rd ? pos - in_rd : RING_FRAMES - in_rd;
        audio_input_push(in_ring + (size_t)in_rd * 2, n);
        in_rd = (in_rd + n) & (RING_FRAMES - 1);
    }
}

void hda_pump(bool silent) {
    silent_mode = silent;
    if (!ring) return;
    pumps++;
    if (in_running) input_drain();
    uint32_t pos = (mmio_r32(sd + SD_LPIB) / 4) & (RING_FRAMES - 1);
    uint32_t filled = (wr_frames - pos) & (RING_FRAMES - 1);
    if (!ready) filled = 0;
    while (filled < lead) {
        int16_t *dst = ring + (size_t)wr_frames * 2;
        render(dst);
        if (silent) memset(dst, 0, CHUNK_FRAMES * 4);
        if (tone) tone_fill(dst, CHUNK_FRAMES);
        wr_frames = (wr_frames + CHUNK_FRAMES) & (RING_FRAMES - 1);
        filled += CHUNK_FRAMES;
    }
}

const struct sound sound_hda = { "HDA", "Intel HDA", hda_rate, RATE, hda_latency, hda_pump, hda_prefill, hda_hold, hda_long_lead, hda_status,
                                 hda_test_tone, hda_poll_jacks, hda_headphones, hda_all_outputs, hda_inputs, hda_input_select, hda_input_jack };
