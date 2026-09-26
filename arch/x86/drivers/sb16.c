/* Sound Blaster 16 (ISA): the DSP at 0x220 (or 0x240, 0x260, 0x280), 16-bit stereo through the card's 16-bit ISA DMA
   channel in auto-init mode, over a ring in the first 16 MB. The card tops out at 44.1 kHz, so the engine keeps
   rendering 48 kHz and each frame going out is interpolated between two of its frames. There is no IRQ handler: the
   pump reads where the DMA is and acknowledges the card's interrupt itself. Older Sound Blasters (8-bit) are left to
   the PC speaker. */
#include "sound.h"
#include "io.h"
#include "mem.h"
#include "pit.h"
#include "audio.h"
#include "libc.h"
#include "log.h"
#include "platform.h"

#define OUT_RATE 44100
#define RING_FRAMES 8192                        /* 32 KiB, 186 ms; aligned to its size, it never crosses a DMA page */
#define LEAD 1024
#define STEP ((uint32_t)((48000ull << 16) / OUT_RATE))   /* engine frames per output frame, Q16 */
#define SRC 128                                 /* engine frames rendered at a time */

static uint16_t base; static int dma;
static int16_t *ring; static uint64_t ring_phys;
static uint32_t wr, lead = LEAD, pumps, frac = SRC << 16, tone_phase;
static bool ready, held, tone;
static int16_t src[(SRC + 1) * 2];              /* engine frames; [0] is the last of the batch before */
static int ver_major, ver_minor;
static const uint8_t addr_port[4] = { 0xC0, 0xC4, 0xC8, 0xCC }, count_port[4] = { 0xC2, 0xC6, 0xCA, 0xCE }, page_port[4] = { 0x8F, 0x8B, 0x89, 0x8A };

static bool dsp_write(uint8_t v) {
    for (int i = 0; i < 65536; i++) if (!(inb(base + 0xC) & 0x80)) { outb(base + 0xC, v); return true; }
    return false;
}
static int dsp_read(void) {
    for (int i = 0; i < 65536; i++) if (inb(base + 0xE) & 0x80) return inb(base + 0xA);
    return -1;
}
static bool dsp_reset(uint16_t b) {
    outb(b + 6, 1);
    for (int i = 0; i < 8; i++) io_wait();                           /* at least 3 µs */
    outb(b + 6, 0);
    for (int i = 0; i < 2000; i++) { if ((inb(b + 0xE) & 0x80) && inb(b + 0xA) == 0xAA) return true; io_wait(); }
    return false;
}
static uint8_t mixer_rd(uint8_t r) { outb(base + 4, r); return inb(base + 5); }
static void mixer_wr(uint8_t r, uint8_t v) { outb(base + 4, r); outb(base + 5, v); }

/* the frame the DMA reads next: from the channel's count of words left */
static uint16_t count(void) {
    int ch = dma & 3;
    outb(0xD8, 0);
    uint16_t c = inb(count_port[ch]);
    return (uint16_t)(c | inb(count_port[ch]) << 8);
}
static uint32_t position(void) {
    uint16_t a = count(), b = count();
    for (int i = 0; i < 3 && (uint16_t)(a - b) > 16; i++) { a = b; b = count(); }   /* a carry between the two bytes: again */
    uint32_t left = ((uint32_t)b + 1) & 0xFFFF;
    if (left > RING_FRAMES * 2) left = RING_FRAMES * 2;
    return ((RING_FRAMES * 2 - left) / 2) & (RING_FRAMES - 1);
}

static void refill(void) {                                          /* the next SRC engine frames after the last one */
    src[0] = src[SRC * 2]; src[1] = src[SRC * 2 + 1];
    if (held) memset(src + 2, 0, SRC * 4); else audio_render(src + 2, SRC);
}

static void pump(bool silent) {
    if (!ready) return;
    pumps++;
    inb(base + 0xF);                                                 /* acknowledge the 16-bit DMA interrupt, if raised */
    uint32_t filled = (wr - position()) & (RING_FRAMES - 1);
    while (filled < lead) {
        if ((frac >> 16) >= SRC) { refill(); frac -= SRC << 16; }
        uint32_t i = frac >> 16; int32_t f = (int32_t)((frac & 0xFFFF) >> 1);   /* Q15: the products stay 32-bit */
        const int16_t *a = src + i * 2, *b = a + 2;
        int16_t *d = ring + (size_t)wr * 2;
        d[0] = (int16_t)(a[0] + (((b[0] - a[0]) * f) >> 15));
        d[1] = (int16_t)(a[1] + (((b[1] - a[1]) * f) >> 15));
        if (silent) d[0] = d[1] = 0;
        if (tone) { tone_phase += 440u * 97391u; d[0] = d[1] = (tone_phase & 0x80000000u) ? 8000 : -8000; }
        frac += STEP;
        wr = (wr + 1) & (RING_FRAMES - 1);
        filled++;
    }
}
static void prefill(void) {
    if (!ready) return;
    uint32_t st = plat_irq_save();
    uint32_t keep = lead; lead = RING_FRAMES - 256;
    pump(false);
    lead = keep;
    plat_irq_restore(st);
}

static uint32_t rate(void) { return 48000; }
static uint32_t latency(void) { return lead * 48000u / OUT_RATE + SRC / 2; }   /* in engine frames */
static void hold(bool on) { held = on; }
static void long_lead(void) { lead = 4096; logf("sb16: %u frames ahead (no timer interrupts)", lead); }
static void test_tone(bool on) { tone = on; }
static void status(char *out, int cap) {
    static uint32_t last_pos, last_pumps;
    uint32_t pos = position();
    snfmt(out, (size_t)cap, "SB16 DSP %d.%02d at %xh, DMA %d · dma %s · pump %s%s", ver_major, ver_minor, base, dma,
          pos != last_pos ? "moving" : "STUCK", pumps != last_pumps ? "running" : "STOPPED", tone ? " · TONE" : "");
    last_pos = pos; last_pumps = pumps;
}

bool sb16_init(void) {
    static const uint16_t bases[] = { 0x220, 0x240, 0x260, 0x280 };
    base = 0;
    for (unsigned i = 0; i < ARRAY_LEN(bases) && !base; i++) if (inb(bases[i] + 0xE) != 0xFF && dsp_reset(bases[i])) base = bases[i];
    if (!base) return false;
    if (!dsp_write(0xE1)) return false;
    ver_major = dsp_read(); ver_minor = dsp_read();
    if (ver_major < 4) { logf("sb16: a Sound Blaster with DSP %d.%02d at %xh: 8-bit only, left to the PC speaker", ver_major, ver_minor, base); return false; }
    uint8_t dmas = mixer_rd(0x81);                                   /* the channels the card is set to; 16-bit in bits 5-7 */
    dma = dmas & 0x20 ? 5 : dmas & 0x40 ? 6 : dmas & 0x80 ? 7 : 0;
    if (!dma) { logf("sb16: no 16-bit DMA channel set (%02x)", dmas); return false; }
    uint64_t p; uint8_t *m = dma_alloc(RING_FRAMES * 4 * 2, &p);     /* 64 KiB: the ring, aligned to its size, inside */
    uint64_t off = ((p + RING_FRAMES * 4 - 1) & ~(uint64_t)(RING_FRAMES * 4 - 1)) - p;
    ring = (int16_t *)(m + off); ring_phys = p + off;
    if (ring_phys + RING_FRAMES * 4 > (16u << 20)) { logf("sb16: no memory below 16 MB for ISA DMA"); return false; }
    mixer_wr(0x30, 0xF0); mixer_wr(0x31, 0xF0);                      /* master, -2 dB */
    mixer_wr(0x32, 0xF8); mixer_wr(0x33, 0xF8);                      /* voice (the DAC), full */
    /* the DMA channel: single mode, auto-init, memory to the card; 16-bit channels count in words */
    int ch = dma & 3; uint32_t words = RING_FRAMES * 2, wa = (uint32_t)(ring_phys >> 1);
    outb(0xD4, (uint8_t)(4 | ch));                                   /* masked while set up */
    outb(0xD8, 0);
    outb(0xD6, (uint8_t)(0x58 | ch));
    outb(addr_port[ch], (uint8_t)wa); outb(addr_port[ch], (uint8_t)(wa >> 8));
    outb(page_port[ch], (uint8_t)((ring_phys >> 16) & 0xFE));
    outb(0xD8, 0);
    outb(count_port[ch], (uint8_t)(words - 1)); outb(count_port[ch], (uint8_t)((words - 1) >> 8));
    outb(0xD4, (uint8_t)ch);
    /* the DSP: the rate, then 16-bit signed stereo, auto-init, in blocks of half the ring (an interrupt each) */
    dsp_write(0x41); dsp_write(OUT_RATE >> 8); dsp_write(OUT_RATE & 0xFF);
    uint32_t block = words / 2 - 1;
    dsp_write(0xB6); dsp_write(0x30); dsp_write((uint8_t)block); dsp_write((uint8_t)(block >> 8));
    wr = 0; ready = true;
    logf("sb16: Sound Blaster 16 (DSP %d.%02d) at %xh, DMA %d, 44.1 kHz from the engine's 48", ver_major, ver_minor, base, dma);
    return true;
}

const struct sound sound_sb16 = { "SB16", "Sound Blaster 16", rate, OUT_RATE, latency, pump, prefill, hold, long_lead, status, test_tone,
                                  0, 0, 0, 0, 0, 0 };
