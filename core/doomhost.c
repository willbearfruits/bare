/* Doom inside BARE! (core/doomhost.h): the memory, the screen, the keys, the clock and the stick's files that Doom's glue
   (third_party/doom/bare) asks for, and starting, leaving and coming back. */
#include "doomhost.h"
#include "app.h"
#include "disk.h"
#include "gfx.h"
#include "log.h"
#include "platform.h"
#include "ui.h"

/* ---- memory: a piece set aside at boot, shared out when Doom starts ---- */
#define HEAP_BYTES   (2u << 20)                /* the C library's malloc */
#define EVENT_BYTES  (1u << 20)                /* a song's events (core/doomsnd.c) */
#define ZONE_MIN     (6u << 20)
#define ZONE_MAX     (32u << 20)
static uint8_t *pool; static uint32_t pool_bytes;
static uint8_t *zone, *heap, *wad; static uint32_t zone_bytes, wad_room;
/* Doom's variables as they were at boot: the linker gathers them (the Makefile renames the sections) */
extern char __start_doom_data[], __stop_doom_data[], __start_doom_bss[], __stop_doom_bss[];
static uint8_t *pristine;

void doom_reserve(uint32_t avail) {
    if (avail < (96u << 20)) { logf("doom: none kept (%u MiB free; it needs 96)", avail >> 20); return; }
    uint32_t want = MIN(avail / 6, 64u << 20), keep = (uint32_t)(__stop_doom_data - __start_doom_data);
    pristine = plat_alloc(keep ? keep : 1);
    pool = pristine ? plat_alloc(want) : 0;
    if (!pool) { logf("doom: no memory"); return; }
    pool_bytes = want;
    memcpy(pristine, __start_doom_data, keep);
    logf("doom: %u MiB set aside, %u KiB of its variables kept", want >> 20, keep >> 10);
}
uint32_t doom_memory(void) { return pool_bytes; }
void *doom_heap(uint32_t *bytes) { *bytes = HEAP_BYTES; return heap; }
void *doom_zone(uint32_t *bytes) { *bytes = zone_bytes; return zone; }

/* ---- state ---- */
enum { OFF, RUNNING, STOPPED };
static int state;
static bool showing, in_engine, quit_asked, leave_asked;
static void *jump[5];                          /* __builtin_setjmp: where doom_fatal comes back to */
static char status[96], wad_path[32];
const char *doom_status(void) { return status; }
bool doom_showing(void) { return showing; }
bool doom_resident(void) { return state == RUNNING; }
bool doom_started(void) { return state != OFF; }

/* ---- the clock: Doom's time passes only while it shows ---- */
static uint64_t clock_base; static uint32_t clock_held = 1;
uint32_t doom_clock_ms(void) { return showing ? (uint32_t)(plat_ms() - clock_base) : clock_held; }
void doom_sleep_ms(int ms) {
    uint64_t end = plat_ms() + (uint64_t)(ms > 0 ? ms : 0);
    while (plat_ms() < end) { app_background(plat_ms()); plat_idle(); }
}

/* ---- keys: a queue; what Doom holds is let go when it is left ---- */
#define KQ 64
static struct { uint8_t code, down; } kq[KQ];
static uint32_t kq_w, kq_r, held[8];
static void push(uint8_t code, bool down) { if (kq_w - kq_r < KQ) { kq[kq_w % KQ].code = code; kq[kq_w % KQ].down = down; kq_w++; } }
void doom_key(uint8_t code, bool down) {
    if (down) held[code >> 5] |= 1u << (code & 31); else held[code >> 5] &= ~(1u << (code & 31));
    push(code, down);
}
bool doom_key_next(uint8_t *code, bool *down) {
    if (kq_r == kq_w) return false;
    *code = kq[kq_r % KQ].code; *down = kq[kq_r % KQ].down; kq_r++;
    return true;
}
static void let_go(void) {
    for (int c = 0; c < 256; c++) if (held[c >> 5] >> (c & 31) & 1) push((uint8_t)c, false);
    memset(held, 0, sizeof held);
}

/* ---- the screen: Doom's 320x200 stretched to 4:3, as large as the screen and the framebuffer's speed allow ---- */
static uint32_t ui_pal[256];                   /* BARE!'s palette, while Doom's is on */
static uint8_t doom_pal[768]; static bool have_pal;
static int bx, by, bw, bh;
static bool fresh;                             /* the first frame after taking the screen clears what was there */
static uint16_t colmap[4096], rowmap[2160];
static void place(void) {
    int W = gfx_width(), H = gfx_height();
    bh = H; bw = H * 4 / 3;
    if (bw > W) { bw = W; bh = W * 3 / 4; }
    /* the whole picture changes as it moves: as large as the screen takes 25 times a second (its speed measured at
       boot; a slow, uncached one gets 640x480 or less), and at most about a 1080p screen's worth (4K: 1600x1200) */
    uint64_t budget = 2100000;
    if (gfx_speed_mbs) budget = MIN(budget, (uint64_t)gfx_speed_mbs * 1000000 / 25 / 4);
    if ((uint64_t)bw * bh > budget) {
        int k = 1; while ((uint64_t)320 * (k + 1) * 240 * (k + 1) <= budget && 320 * (k + 1) <= W && 240 * (k + 1) <= H) k++;
        bw = 320 * k; bh = 240 * k;
    }
    bw = MIN(bw, 4096); bh = MIN(bh, 2160);
    bx = (W - bw) / 2; by = (H - bh) / 2;
    for (int x = 0; x < bw; x++) colmap[x] = (uint16_t)(x * 320 / bw);
    for (int y = 0; y < bh; y++) rowmap[y] = (uint16_t)(y * 200 / bh);
}
static void take_screen(void) {
    for (int i = 0; i < 256; i++) ui_pal[i] = gfx_rgb((uint8_t)i);
    place();
    gfx_pointer(0, 0, PTR_HIDDEN);
    if (have_pal) doom_palette(doom_pal);
    gfx_noclip();
    gfx_fill(0, 0, gfx_width(), gfx_height(), 0);    /* Doom's colour 0 is black */
    fresh = true;
}
static void give_screen(void) {
    for (int i = 0; i < 256; i++) gfx_color(i, ui_pal[i] >> 16 & 255, ui_pal[i] >> 8 & 255, ui_pal[i] & 255);
    gfx_repaint();
    ui_redraw_all();
}
void doom_palette(const uint8_t *rgb) {
    if (rgb != doom_pal) { memcpy(doom_pal, rgb, 768); have_pal = true; }
    for (int i = 0; i < 256; i++) gfx_color(i, rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]);
    gfx_repaint();
}
void doom_frame(const uint8_t *s) {
    if (!showing) return;
    if (fresh) { fresh = false; gfx_noclip(); gfx_fill(0, 0, gfx_width(), gfx_height(), 0); }   /* the loading bar's words */
    for (int y = 0; y < bh; y++) {
        uint8_t *d = gfx_row(by + y) + bx;
        if (y && rowmap[y] == rowmap[y - 1]) { memcpy(d, gfx_row(by + y - 1) + bx, (size_t)bw); continue; }
        const uint8_t *row = s + rowmap[y] * 320;
        for (int x = 0; x < bw; x++) d[x] = row[colmap[x]];
    }
    gfx_dirty(bx, by, bw, bh);
    gfx_present();
}

/* ---- what the engine prints goes to the log, a line at a time ---- */
static char line[160]; static int line_len;
void doom_print(const char *t) {
    for (; *t; t++) {
        if (*t == '\n' || line_len == (int)sizeof line - 1) {
            line[line_len] = 0;
            if (line_len) logf("doom: %s", line);
            line_len = 0;
            if (*t == '\n') continue;
        }
        if (*t != '\r') line[line_len++] = *t;
    }
}

/* ---- files: Doom's folder on the stick (DOOM/, else the root) ---- */
static char folder[8];
static bool path_for(const char *name, char *out, int cap) {
    if (!disk.have_boot_fat) return false;
    while (name[0] == '.' && name[1] == '/') name += 2;
    if (strlen(name) + 8 > (size_t)cap) return false;
    bool rooted = false;
    for (const char *p = name; *p; p++) if (*p == '/') rooted = true;
    if (rooted || !folder[0]) snfmt(out, (size_t)cap, "%s", name[0] == '/' ? name + 1 : name);
    else snfmt(out, (size_t)cap, "%s/%s", folder, name);
    return true;
}
int32_t doom_file_size(const char *name) {
    char p[80]; struct fat_file fi;
    if (!path_for(name, p, sizeof p) || !fat_find(&disk.fat, p, &fi) || (!fi.size && fi.first)) return -1;   /* not a folder */
    return (int32_t)fi.size;
}
bool doom_file_read(const char *name, uint32_t off, void *dst, uint32_t len) {
    char p[80]; struct fat_file fi;
    return path_for(name, p, sizeof p) && fat_find(&disk.fat, p, &fi) && fat_read(&disk.fat, &fi, off, dst, len);
}
bool doom_file_write(const char *name, const void *src, uint32_t len) {
    char p[80]; struct fat_file fi;
    if (!path_for(name, p, sizeof p)) return false;
    char *slash = 0;
    for (char *q = p; *q; q++) if (*q == '/') slash = q;
    const char *dir = "", *base = p;
    if (slash) { *slash = 0; dir = p; base = slash + 1; }
    bool ok = fat_create(&disk.fat, dir, base, len, &fi) && (!len || fat_write_inplace(&disk.fat, &fi, 0, src, len));
    logf("doom: %s %s/%s (%u bytes)", ok ? "wrote" : "could not write", dir, base, len);
    return ok;
}

/* the WAD: every byte into memory once (a bar shows how far), then the engine reads it in place */
void *doom_wad_load(const char *name, uint32_t *bytes) {
    char p[80]; struct fat_file fi;
    if (!path_for(name, p, sizeof p) || !fat_find(&disk.fat, p, &fi) || !fi.size || fi.size > wad_room) return 0;
    struct fat_cursor c; fat_rewind(&fi, &c);
    uint32_t piece = 1u << 20;
    for (uint32_t off = 0; off < fi.size; off += piece) {
        char m[64]; snfmt(m, sizeof m, "reading %s: %u of %u MB", p, off >> 20, (fi.size + (1u << 20) - 1) >> 20);
        ui_boot_message("DOOM", m);
        if (!fat_seq(&disk.fat, &c, wad + off, MIN(piece, fi.size - off), false)) { logf("doom: could not read %s", p); return 0; }
    }
    *bytes = fi.size;
    return wad;
}

/* The IWADs it knows, in the order it prefers them; then any other IWAD in the folder or the root */
static const char *const known[] = { "DOOM.WAD", "DOOM2.WAD", "PLUTONIA.WAD", "TNT.WAD", "DOOMU.WAD", "DOOM1.WAD",
                                     "freedoom1.wad", "freedoom2.wad" };
static bool is_iwad(const char *path, struct fat_file *fi) {
    uint8_t h[12];
    return fat_find(&disk.fat, path, fi) && fi->size >= 12 && fat_read(&disk.fat, fi, 0, h, 12) && memcmp(h, "IWAD", 4) == 0;
}
/* the IWAD's path as fat_find takes it ("DOOM/DOOM.WAD", "freedoom1.wad"), and whether it is in the DOOM folder */
static bool find_iwad_path(char *out, int cap, uint32_t *size, bool *in_folder) {
    struct fat_file fi; char p[40];
    for (int d = 0; d < 2; d++) {
        const char *dir = d == 0 ? "DOOM" : "";
        for (unsigned i = 0; i < ARRAY_LEN(known); i++) {
            snfmt(p, sizeof p, "%s%s%s", dir, *dir ? "/" : "", known[i]);
            if (is_iwad(p, &fi)) { snfmt(out, (size_t)cap, "%s", p); *size = fi.size; *in_folder = d == 0; return true; }
        }
        struct fat_entry e[16];
        int n = fat_list(&disk.fat, dir, "WAD", e, 16);
        for (int i = 0; i < n; i++) {
            snfmt(p, sizeof p, "%s%s%s", dir, *dir ? "/" : "", e[i].name);
            if (is_iwad(p, &fi)) { snfmt(out, (size_t)cap, "%s", p); *size = fi.size; *in_folder = d == 0; return true; }
        }
    }
    return false;
}
/* as the engine names it: inside Doom's folder by its name, else from the root */
static bool find_iwad(char *out, int cap, uint32_t *size) {
    char p[40]; bool in_folder;
    if (!find_iwad_path(p, sizeof p, size, &in_folder)) return false;
    if (in_folder && folder[0]) snfmt(out, (size_t)cap, "%s", p + strlen(folder) + 1);
    else snfmt(out, (size_t)cap, "/%s", p);
    return true;
}
bool doom_wad_path(char *out, int cap, uint32_t *size) { bool in_folder; return disk.have_boot_fat && find_iwad_path(out, cap, size, &in_folder); }

/* ---- starting, stopping, leaving, coming back ---- */
static void stopped(void) {
    in_engine = false;
    state = STOPPED;
    doomsnd_all_off();
    if (showing) { showing = false; give_screen(); }
    ui_notice(status, plat_ms());
}
void doom_fatal(const char *msg) {
    doom_print("\n");
    snfmt(status, sizeof status, "Doom stopped: %s", msg);
    logf("doom: %s", status);
    __builtin_longjmp(jump, 1);
}
void doom_quit(void) { quit_asked = true; }

static void say(const char *m, uint64_t now) { snfmt(status, sizeof status, "%s", m); logf("doom: %s", m); ui_notice(m, now); }

void doom_open(uint64_t now) {
    if (showing) return;
    if (state == RUNNING) {                                        /* back where it was, paused at its menu */
        showing = true; clock_base = plat_ms() - clock_held;
        take_screen();
        doomsnd_pause(false);
        if (__builtin_setjmp(jump)) { stopped(); return; }
        in_engine = true;
        doom_engine_resume();
        in_engine = false;
        return;
    }
    if (!pool) { say("Doom: no memory to spare for it on this machine", now); return; }
    if (!disk.have_boot_fat) { say("Doom: no stick to find its WAD on", now); return; }
    struct fat_file d;
    folder[0] = 0;
    if (fat_find(&disk.fat, "DOOM", &d) && !d.size && d.first) snfmt(folder, sizeof folder, "DOOM");
    uint32_t size;
    if (!find_iwad(wad_path, sizeof wad_path, &size)) { say("Doom: no WAD on the stick (put one in its DOOM folder)", now); return; }
    if ((uint64_t)size + HEAP_BYTES + EVENT_BYTES + ZONE_MIN > pool_bytes) {
        char m[96]; snfmt(m, sizeof m, "Doom: its WAD needs %u MB, this machine kept %u", (size + HEAP_BYTES + EVENT_BYTES + ZONE_MIN) >> 20, pool_bytes >> 20);
        say(m, now); return;
    }
    /* the pool: heap, the WAD, a song's events, the zone */
    heap = pool;
    wad = heap + HEAP_BYTES; wad_room = (size + 4095) & ~4095u;
    uint8_t *events = wad + wad_room;
    zone = events + EVENT_BYTES; zone_bytes = MIN(pool_bytes - (uint32_t)(zone - pool), ZONE_MAX);
    /* a fresh start: Doom's variables as they were at boot */
    doomsnd_all_off();
    doomsnd_set_memory(events, EVENT_BYTES);
    memcpy(__start_doom_data, pristine, (size_t)(__stop_doom_data - __start_doom_data));
    memset(__start_doom_bss, 0, (size_t)(__stop_doom_bss - __start_doom_bss));
    kq_r = kq_w = 0; memset(held, 0, sizeof held);
    have_pal = false; quit_asked = leave_asked = false; line_len = 0;
    logf("doom: starting with %s%s%s (%u KiB), zone %u MiB", folder, folder[0] && wad_path[0] != '/' ? "/" : "", wad_path, size >> 10, zone_bytes >> 20);
    snfmt(status, sizeof status, "Doom: %s", wad_path);
    showing = true; clock_held = 1; clock_base = plat_ms() - clock_held;
    take_screen();
    if (__builtin_setjmp(jump)) { stopped(); return; }
    in_engine = true;
    state = RUNNING;
    doom_engine_start(wad_path);
    in_engine = false;
    if (quit_asked || leave_asked) { quit_asked = leave_asked = false; doom_leave(); }
}

void doom_step(uint64_t now) {
    if (!showing || state != RUNNING) return;
    if (__builtin_setjmp(jump)) { stopped(); return; }
    in_engine = true;
    doom_engine_tick();
    in_engine = false;
    if (quit_asked || leave_asked) { quit_asked = leave_asked = false; doom_leave(); }
}

void doom_leave(void) {
    if (!showing) return;
    if (in_engine) { leave_asked = true; return; }             /* in the middle of a tic: once it is over */
    clock_held = doom_clock_ms();
    showing = false;
    let_go();
    doomsnd_pause(true);
    give_screen();
}
