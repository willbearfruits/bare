/* The splash's frame: which piece, its sclock, skipping, and what the pieces share (the name in ASCII, captions,
   colours, a reverb). See splash.h. */
#include "splash_int.h"
#include "app.h"
#include "audio.h"
#include "platform.h"
#include "log.h"

int splash_mode = SPLASH_RANDOM;
const char *const splash_names[SPLASHES] = { "ANS", "GENDY", "CMI", "METASTASEIS" };
static const struct splash_piece *const pieces[SPLASHES] = { &splash_ans, &splash_gendy, &splash_cmi, &splash_meta };

/* figlet's slant font, the letters kerned a column apart (figlet's own smushing merges B into A) */
const char *const splash_logo[LOGO_ROWS] = {
    "    ____    ___      ____    ______  __",
    "   / __ )  /   |    / __ \\  / ____/ / /",
    "  / __  | / /| |   / /_/ / / __/   / / ",
    " / /_/ / / ___ |  / _, _/ / /___  /_/  ",
    "/_____/ /_/  |_| /_/ |_| /_____/ (_)   ",
};
struct splash_geo sg;

static const struct splash_piece *volatile cur;     /* the piece, while its picture or sound runs */
static volatile uint32_t sclock;                     /* audio frames since the start: picture and sound follow it */
static volatile uint32_t fade_from;                  /* a skip: the frame the fade began at (~0: none) */
static volatile bool sounding;
static uint32_t rate, sound_end, fade_len;
static bool showing, first;
static int32_t pl[64], pr[64];                       /* the piece renders here; added to the mix with the fade */

int splash_pick(int last) {
    uint64_t us = plat_us();
    uint32_t s = (uint32_t)us * 2654435761u ^ (uint32_t)(us >> 17) * 40503u;
    struct rtc_time t;
    if (plat_rtc(&t)) s ^= (uint32_t)(t.sec + 60 * t.min + 3600 * t.hour + 86400 * t.day) * 2246822519u;
    s ^= s >> 15; s *= 2654435761u; s ^= s >> 13;
    if (last < 0 || last >= SPLASHES) return (int)(s % SPLASHES);
    int k = (int)(s % (SPLASHES - 1));
    return k >= last ? k + 1 : k;
}

static void geometry(void) {
    sg.W = gfx_width(); sg.H = gfx_height(); sg.f = text_font();
    sg.scale = MAX(1, sg.W * 6 / 10 / (LOGO_COLS * sg.f->width));
    while (sg.scale > 1 && LOGO_ROWS * sg.f->height * sg.scale > sg.H / 3) sg.scale--;
    sg.cw = sg.f->width * sg.scale; sg.ch = sg.f->height * sg.scale;
    sg.lx = (sg.W - LOGO_COLS * sg.cw) / 2; sg.ly = (sg.H - LOGO_ROWS * sg.ch) / 2;
}

void splash_start(int which) {
    if (which < 0 || which >= SPLASHES) return;
    const struct splash_piece *p = pieces[which];
    rate = audio_rate();
    geometry();
    gfx_noclip();
    p->start(rate);
    sound_end = (uint32_t)((uint64_t)(p->ms + p->tail_ms) * rate / 1000);
    fade_len = rate * 3 / 20;
    fade_from = ~0u; sclock = 0; showing = true; first = true;
    gfx_pointer(0, 0, PTR_HIDDEN);
    uint32_t f = plat_irq_save(); cur = p; sounding = true; plat_irq_restore(f);
    logf("splash: %s", p->name);
}

bool splash_showing(void) { return showing; }

void splash_skip(void) {
    if (!showing) return;
    showing = false;
    fade_from = sclock;
    logf("splash: skipped at %u ms", (unsigned)((uint64_t)sclock * 1000 / rate));
}

void splash_draw(void) {
    const struct splash_piece *p = cur;
    if (!showing || !p) return;
    int t = (int)((uint64_t)sclock * 1000 / rate);
    if (t >= p->ms) { showing = false; logf("splash: done"); return; }
    p->draw(t, first);
    first = false;
}

void splash_audio(int32_t *l, int32_t *r, uint32_t n) {
    const struct splash_piece *p = cur;
    if (!p || !sounding) return;
    uint32_t t = sclock, ff = fade_from;
    if (t >= sound_end || (ff != ~0u && t >= ff + fade_len)) { sounding = false; return; }
    if (ff == ~0u && t + n + fade_len >= sound_end) fade_from = ff = sound_end - fade_len;   /* the tail ends in a fade too */
    for (uint32_t done = 0; done < n;) {
        uint32_t k = MIN(n - done, 64u);
        memset(pl, 0, sizeof pl); memset(pr, 0, sizeof pr);
        p->audio(pl, pr, k, t);
        if (ff == ~0u) for (uint32_t i = 0; i < k; i++) { l[done + i] += pl[i]; r[done + i] += pr[i]; }
        else for (uint32_t i = 0; i < k; i++) {                 /* a skip: down to nothing over fade_len */
            uint32_t e = t + i - ff;
            int32_t g = e >= fade_len ? 0 : (int32_t)(32767 - (uint64_t)e * 32767 / fade_len);
            l[done + i] += (pl[i] * (g >> 3)) >> 12; r[done + i] += (pr[i] * (g >> 3)) >> 12;
        }
        done += k; t += k;
    }
    sclock = t;
}

/* ---- shared pieces ---- */
void splash_glyph(int px, int py, char c, uint8_t color, int scale) { gfx_glyph(px, py, sg.f, (uint8_t)c, color, -1, scale); }

void splash_captions(const char *caption, uint8_t color) {
    const struct font *f = sg.f;
    char v[40]; snfmt(v, sizeof v, "BARE! %s", BARE_RELEASE BARE_STAGE);
    int y = sg.H - f->height - f->height / 2;
    gfx_text(f->width * 2, y, v, f, color, -1, 1);
    if (caption) gfx_text(sg.W - f->width * 2 - gfx_text_width(caption, f, 1), y, caption, f, color, -1, 1);
}

void splash_ramp(int first, int n, uint32_t from, uint32_t to) {
    for (int i = 0; i < n; i++) {
        int c[3];
        for (int k = 0; k < 3; k++) {
            int a = (int)(from >> (16 - 8 * k) & 255), b = (int)(to >> (16 - 8 * k) & 255);
            c[k] = a + (b - a) * i / (n > 1 ? n - 1 : 1);
        }
        gfx_color(first + i, c[0], c[1], c[2]);
    }
}

uint32_t sp_inc(uint32_t hz_q8, uint32_t r) { return (uint32_t)(((uint64_t)hz_q8 << 24) / r); }

static int32_t verb_mem[4][4800];                    /* 100 ms a line at 48 kHz */
void sp_verb_init(struct sp_verb *v, uint32_t r, int fb_q15, int damp_q15) {
    static const uint16_t ms_x10[4] = { 537, 719, 853, 971 };       /* mutually prime-ish lengths, 54 to 97 ms */
    memset(verb_mem, 0, sizeof verb_mem);
    for (int k = 0; k < 4; k++) { v->line[k] = verb_mem[k]; v->len[k] = MIN(4800u, ms_x10[k] * r / 10000); v->lp[k] = 0; v->pos[k] = 0; }
    v->fb = fb_q15; v->damp = damp_q15;
}
void sp_verb_run(struct sp_verb *v, const int32_t *inl, const int32_t *inr, int32_t *outl, int32_t *outr, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        int32_t a[4];
        for (int k = 0; k < 4; k++) {
            a[k] = v->line[k][v->pos[k]];
            v->lp[k] += ((a[k] - v->lp[k]) * (v->damp >> 3)) >> 12;
        }
        int32_t s0 = v->lp[0] + v->lp[1], d0 = v->lp[0] - v->lp[1], s1 = v->lp[2] + v->lp[3], d1 = v->lp[2] - v->lp[3];
        int32_t h[4] = { (s0 + s1) >> 1, (d0 + d1) >> 1, (s0 - s1) >> 1, (d0 - d1) >> 1 };
        int32_t in[4] = { inl[i], inr[i], inl[i], inr[i] };
        for (int k = 0; k < 4; k++) {
            int32_t w = in[k] + ((CLAMP(h[k], -65535, 65535) * (v->fb >> 3)) >> 12);
            v->line[k][v->pos[k]] = CLAMP(w, -65535, 65535);
            if (++v->pos[k] >= v->len[k]) v->pos[k] = 0;
        }
        outl[i] += (a[0] + a[2]) >> 1; outr[i] += (a[1] + a[3]) >> 1;
    }
}
