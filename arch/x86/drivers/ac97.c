/* AC'97: Intel's ICH and the chipsets that copied its bus master (nForce, AMD 768/8111, SiS 7012 with two registers
   swapped). Output is a 32-entry buffer list over one ring, its last valid entry kept just behind the current one so
   the DMA never runs out; the codec plays 48 kHz, which every AC'97 codec does without variable rate. Line in and the
   microphone come in through the PCM-in list the same way, into the engine's input FIFO. */
#include "sound.h"
#include "io.h"
#include "pci.h"
#include "mem.h"
#include "pit.h"
#include "audio.h"
#include "libc.h"
#include "log.h"
#include "platform.h"

#define RATE 48000
#define ENTRIES 32
#define ENTRY_FRAMES 128
#define RING_FRAMES (ENTRIES * ENTRY_FRAMES)    /* 4096: 85 ms */
#define LEAD 512
#define CHUNK 128

/* the codec's mixer (NAM, I/O BAR 0) */
enum { M_RESET = 0x00, M_MASTER = 0x02, M_HP = 0x04, M_MONO = 0x06, M_BEEP = 0x0A, M_PHONE = 0x0C, M_MIC = 0x0E,
       M_LINE = 0x10, M_CD = 0x12, M_VIDEO = 0x14, M_AUX = 0x16, M_PCM = 0x18, M_REC_SEL = 0x1A, M_REC_GAIN = 0x1C,
       M_POWER = 0x26, M_EXT_ID = 0x28, M_EXT_CTL = 0x2A, M_FRONT_RATE = 0x2C, M_ADC_RATE = 0x32, M_VID1 = 0x7C, M_VID2 = 0x7E };
/* the bus master (NABM, I/O BAR 1): a box of registers per DMA engine */
enum { B_IN = 0x00, B_OUT = 0x10 };
enum { BDBAR = 0x00, CIV = 0x04, LVI = 0x05, CR = 0x0B, GLOB_CNT = 0x2C, GLOB_STA = 0x30 };

struct bde { uint32_t addr; uint16_t samples, flags; };

static uint16_t nam, nabm, sr_reg = 0x06, picb_reg = 0x08;
static int16_t *ring, *in_ring;
static struct bde *bdl, *in_bdl;
static uint64_t ring_phys, in_ring_phys, bdl_phys, in_bdl_phys;
static uint32_t wr, lead = LEAD, in_rd, in_frames, pumps, tone_phase;
static bool ready, held, tone, in_running;
static int in_active = -1;
static uint16_t vendor, device; static uint32_t codec_id;

static const char *const in_names[2] = { "LINE IN", "MIC" };

static uint16_t mix_rd(uint16_t r) { return inw(nam + r); }
static void mix_wr(uint16_t r, uint16_t v) { outw(nam + r, v); }

/* where a box's DMA is in its ring, in frames: the current entry and what is left of it */
static uint32_t position(uint16_t box) {
    uint8_t civ = 0; uint16_t picb = 0;
    for (int i = 0; i < 3; i++) {
        civ = inb(nabm + box + CIV) & 31;
        picb = inw(nabm + box + picb_reg);
        if ((inb(nabm + box + CIV) & 31) == civ) break;
    }
    uint32_t left = MIN(picb / 2u, (uint32_t)ENTRY_FRAMES);
    return (civ * ENTRY_FRAMES + ENTRY_FRAMES - left) & (RING_FRAMES - 1);
}
/* the last valid entry just behind the current one: the list never ends; a halted engine is started again */
static void keep_running(uint16_t box) {
    outb(nabm + box + LVI, (uint8_t)(((inb(nabm + box + CIV) & 31) + ENTRIES - 1) & 31));
    if (inw(nabm + box + sr_reg) & 1) { outw(nabm + box + sr_reg, 0x1C); outb(nabm + box + CR, 1); }
}
static void box_start(uint16_t box, uint64_t list_phys) {
    outb(nabm + box + CR, 0);
    outb(nabm + box + CR, 2);                                        /* reset its registers */
    uint64_t end = pit_ticks() + 50;
    while ((inb(nabm + box + CR) & 2) && pit_ticks() < end) ;
    outl(nabm + box + BDBAR, (uint32_t)list_phys);
    outb(nabm + box + LVI, ENTRIES - 1);
    outw(nabm + box + sr_reg, 0x1C);                                 /* clear the status bits */
    outb(nabm + box + CR, 1);                                        /* run, no interrupts */
}
static void make_list(struct bde *list, uint64_t ring_at) {
    for (int i = 0; i < ENTRIES; i++) list[i] = (struct bde){ (uint32_t)(ring_at + (uint64_t)i * ENTRY_FRAMES * 4), ENTRY_FRAMES * 2, 0 };
}

/* the test tone: a 440 Hz square at -12 dB over what the engine rendered */
static void tone_fill(int16_t *d, int frames) {
    for (int i = 0; i < frames; i++) { tone_phase += 440u * 89478u; d[i * 2] = d[i * 2 + 1] = (tone_phase & 0x80000000u) ? 8000 : -8000; }
}

static void in_drain(void) {
    uint32_t pos = position(B_IN);
    while (in_rd != pos) {
        uint32_t n = pos > in_rd ? pos - in_rd : RING_FRAMES - in_rd;
        audio_input_push(in_ring + (size_t)in_rd * 2, n);
        in_rd = (in_rd + n) & (RING_FRAMES - 1);
        in_frames += n;
    }
    keep_running(B_IN);
}

static void pump(bool silent) {
    if (!ready) return;
    pumps++;
    if (in_running) in_drain();
    uint32_t filled = (wr - position(B_OUT)) & (RING_FRAMES - 1);
    while (filled < lead) {
        int16_t *d = ring + (size_t)wr * 2;
        if (held) memset(d, 0, CHUNK * 4); else audio_render(d, CHUNK);
        if (silent) memset(d, 0, CHUNK * 4);
        if (tone) tone_fill(d, CHUNK);
        wr = (wr + CHUNK) & (RING_FRAMES - 1);
        filled += CHUNK;
    }
    keep_running(B_OUT);
}
static void prefill(void) {
    if (!ready) return;
    uint32_t st = plat_irq_save();
    uint32_t keep = lead; lead = RING_FRAMES - 2 * CHUNK;
    pump(false);
    lead = keep;
    plat_irq_restore(st);
}

static int inputs(const char **names, int max) { int n = 0; for (int i = 0; i < 2 && n < max; i++) names[n++] = in_names[i]; return n; }
static bool input_select(int i) {
    if (!ready) return i < 0;
    if (in_running) { outb(nabm + B_IN + CR, 0); in_running = false; }
    in_active = -1;
    if (i < 0) return true;
    if (i > 1) return false;
    if (!in_ring) {
        in_ring = dma_alloc(RING_FRAMES * 4, &in_ring_phys);
        in_bdl = dma_alloc(ENTRIES * sizeof *in_bdl, &in_bdl_phys);
        make_list(in_bdl, in_ring_phys);
    }
    mix_wr(M_REC_SEL, i == 0 ? 0x0404 : 0x0000);                     /* record from line in, or the microphone */
    mix_wr(M_REC_GAIN, 0x0000);                                      /* 0 dB, unmuted */
    mix_wr(M_MIC, i == 1 ? 0x8048 : 0x8008);                         /* out of the analog mix; +20 dB for the mic */
    in_rd = 0;
    box_start(B_IN, in_bdl_phys);
    in_running = true; in_active = i;
    logf("ac97: recording from %s", in_names[i]);
    return true;
}
static bool input_jack(int i) { (void)i; return false; }

static uint32_t rate(void) { return RATE; }
static uint32_t latency(void) { return lead + CHUNK / 2; }
static void hold(bool on) { held = on; }
static void long_lead(void) { lead = 2048; logf("ac97: %u frames ahead (no timer interrupts)", lead); }
static void test_tone(bool on) { tone = on; }
static void status(char *out, int cap) {
    static uint32_t last_pos, last_pumps;
    uint32_t pos = position(B_OUT);
    int n = snfmt(out, (size_t)cap, "AC97 %04x:%04x codec %08x · dma %s · pump %s%s", vendor, device, codec_id, pos != last_pos ? "moving" : "STUCK",
                  pumps != last_pumps ? "running" : "STOPPED", tone ? " · TONE" : "");
    if (in_running) snfmt(out + n, (size_t)(cap - n), " · %s in: %u frames, at %u", in_names[in_active], in_frames, position(B_IN));
    last_pos = pos; last_pumps = pumps;
}

static bool ich4_or_later(uint16_t id) { return id == 0x24C5 || id == 0x24D5 || id == 0x25A6 || id == 0x266E || id == 0x27DE || id == 0x2698; }

static bool try_dev(const struct pci_dev *d) {
    if (d->vendor != 0x8086 && d->vendor != 0x10DE && d->vendor != 0x1022 && !(d->vendor == 0x1039 && d->device == 0x7012)) return false;
    bool m0, m1;
    if (d->vendor == 0x8086 && ich4_or_later(d->device)) {          /* ICH4 on: the old I/O BARs can be switched off (IOSE) */
        uint32_t cfg = pci_read32(d->bus, d->dev, d->fn, 0x40);
        if (!(cfg & 0x100)) pci_write32(d->bus, d->dev, d->fn, 0x40, cfg | 0x100);
    }
    uint64_t b0 = pci_bar(d, 0, &m0), b1 = pci_bar(d, 1, &m1);
    if (m0 || m1 || !b0 || !b1) return false;                        /* newer Intel audio (memory BARs) is HDA's */
    pci_enable(d);
    pci_write32(d->bus, d->dev, d->fn, 4, pci_read32(d->bus, d->dev, d->fn, 4) | 1);   /* I/O space too */
    nam = (uint16_t)b0; nabm = (uint16_t)b1; vendor = d->vendor; device = d->device;
    if (d->vendor == 0x1039) { sr_reg = 0x08; picb_reg = 0x06; }     /* SiS 7012 swaps these two */
    /* cold reset of the AC-link, then wait for the codec */
    uint32_t gc = inl(nabm + GLOB_CNT) & ~(0x2u | 0x8u | 0xF00000u);
    outl(nabm + GLOB_CNT, gc);
    sleep_ms(1);
    outl(nabm + GLOB_CNT, gc | 0x2);
    uint64_t end = pit_ticks() + 1000;
    while (!(inl(nabm + GLOB_STA) & 0x100) && pit_ticks() < end) ;
    if (!(inl(nabm + GLOB_STA) & 0x100)) { logf("ac97: %04x:%04x: no codec answered", vendor, device); return false; }
    mix_wr(M_RESET, 0);
    end = pit_ticks() + 500;
    while ((mix_rd(M_POWER) & 0xF) != 0xF && pit_ticks() < end) ;   /* DAC, ADC, mixer, reference ready */
    codec_id = (uint32_t)mix_rd(M_VID1) << 16 | mix_rd(M_VID2);
    mix_wr(M_MASTER, 0x0000); mix_wr(M_HP, 0x0000); mix_wr(M_MONO, 0x8000); mix_wr(M_PCM, 0x0808);
    mix_wr(M_BEEP, 0x8000); mix_wr(M_PHONE, 0x8008); mix_wr(M_MIC, 0x8008);                   /* inputs out of the analog mix */
    mix_wr(M_LINE, 0x8808); mix_wr(M_CD, 0x8808); mix_wr(M_VIDEO, 0x8808); mix_wr(M_AUX, 0x8808);
    if (mix_rd(M_EXT_ID) & 1) {                                      /* variable rate: set 48 kHz outright */
        mix_wr(M_EXT_CTL, mix_rd(M_EXT_CTL) | 1);
        mix_wr(M_FRONT_RATE, RATE); mix_wr(M_ADC_RATE, RATE);
    }
    ring = dma_alloc(RING_FRAMES * 4, &ring_phys);
    bdl = dma_alloc(ENTRIES * sizeof *bdl, &bdl_phys);
    make_list(bdl, ring_phys);
    wr = 0;
    box_start(B_OUT, bdl_phys);                                      /* the ring starts silent; the first pump fills it */
    ready = true;
    logf("ac97: %04x:%04x at %02x:%02x.%d, codec %08x, io %x/%x, 48 kHz", vendor, device, d->bus, d->dev, d->fn, codec_id, nam, nabm);
    return true;
}

bool ac97_init(void) {
    struct pci_dev devs[4]; int n = pci_find_all_class(0x04, 0x01, devs, 4);
    for (int i = 0; i < n; i++) if (try_dev(&devs[i])) return true;
    return false;
}

const struct sound sound_ac97 = { "AC97", "AC97", rate, RATE, latency, pump, prefill, hold, long_lead, status, test_tone,
                                  0, 0, 0, inputs, input_select, input_jack };
