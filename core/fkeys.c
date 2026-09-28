/* What each F key opens, and KEYS.TXT (see fkeys.h). */
#include "fkeys.h"
#include "ui.h"
#include "xen.h"
#include "lineage.h"
#include "inst.h"
#include "disk.h"
#include "fat.h"
#include "song.h"
#include "install.h"
#include "log.h"
#include "libc.h"

#define PG(p) { .kind = FK_PAGE, .page = (p), .left_at = -1 }
#define XEN_KEY { .kind = FK_VIEW, .page = PAGE_LINEAGE, .view = LV_XEN, .left_at = -1 }     /* F12's */
struct fkey fkeys[FKEYS] = { PG(0), PG(1), PG(2), PG(3), PG(4), PG(5), PG(6), PG(7), PG(8), PG(9), PG(10), XEN_KEY };
const char *const fkey_names[FKEYS] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
char fkeys_status[96] = "the default keys";
bool fkeys_trouble;

/* the pages by PAGE_*: their names (PLAY's tab takes an instrument's, so not ui_pages' names), the title bar's short
   ones, and what they are */
static const char *const page_name[PAGE_COUNT] = { "PLAY", "SEQ", "WAVE", "STRETCH", "OPERATOR", "TAPE", "FILE", "MIX",
                                                   "TOUCH", "FX", "LINEAGE" };
static const char *const page_short[PAGE_COUNT] = { 0, 0, 0, "STR", "FM", 0, 0, 0, 0, 0, "LIN" };
static const char *const page_about[PAGE_COUNT] = {
    "the omnichord and its rhythm section; again: the instruments", "the tracker",
    "waves: a 3D stack, harmonics, drawing, the sampler", "what you played, frozen and stretched",
    "4-operator FM patches", "the 8-track tape", "projects, songs, MIDI, these keys, the log", "the mixer",
    "the crackle box: fingers close the circuit", "effects played live",
    "homages: Xenakis, ANS, Reich, Carlos, Radigue, Merzbow; again: the next" };
static const char *const xen_about[XV_COUNT] = {
    "XENAKIS: string glissandi strung between two lines", "XENAKIS: masses of notes set by probabilities",
    "XENAKIS: scales and rhythms from residue classes", "XENAKIS: draw arcs of pitch over time", "XENAKIS: waveforms that walk at random" };
static const char *const lin_about[LV_COUNT] = {
    "LINEAGE: Xenakis's Metastaseis, clouds, sieves, UPIC, GENDY; again: the next",
    "LINEAGE: Murzin's ANS, a plate of 360 tones", "LINEAGE: Reich's phasing, a pattern against itself",
    "LINEAGE: Carlos's Moog, and her scales without octaves", "LINEAGE: Radigue's slow beating drones",
    "LINEAGE: Merzbow's noise, junk as an instrument" };
static const char *view_about(const struct fkey *k) {
    return k->sub && k->sub <= XV_COUNT ? xen_about[k->sub - 1] : k->page == PAGE_LINEAGE && k->view < LV_COUNT ? lin_about[k->view] : "";
}

static char up(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }
static bool same_prefix(const char *a, int n, const char *b) {         /* a[0..n) starts b, any case */
    for (int i = 0; i < n; i++) if (!b[i] || up(a[i]) != up(b[i])) return false;
    return true;
}
static bool same_word(const char *a, int n, const char *b) { return same_prefix(a, n, b) && b[n] == 0; }
static const char *part(const char *s, int n, int max) {              /* s[0..n), at most max, as a string */
    static char b[2][24]; static int w; char *o = b[w ^= 1]; n = MIN(n, MIN(max, 23));
    memcpy(o, s, (size_t)n); o[n] = 0; return o;
}
static int find_inst(const char *name) {
    for (int i = 0; i < inst_count; i++) if (same_word(name, (int)strlen(name), insts[i].name)) return i;
    return -1;
}

void fkeys_default(struct fkey out[FKEYS]) {
    for (int k = 0; k < FKEYS; k++)
        out[k] = k < PAGE_COUNT ? (struct fkey){ .kind = FK_PAGE, .page = (uint8_t)k, .left_at = -1 } : (struct fkey)XEN_KEY;
}
int fkey_page(const struct fkey *k) {
    return k->kind == FK_PAGE || k->kind == FK_VIEW ? k->page : k->kind == FK_INST ? PAGE_PLAY : -1;
}
bool fkey_same(const struct fkey *a, const struct fkey *b) {
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case FK_PAGE: return a->page == b->page;
    case FK_VIEW: return a->page == b->page && a->view == b->view && a->sub == b->sub;
    case FK_INST: return same_word(a->inst, (int)strlen(a->inst), b->inst);
    }
    return true;
}
const char *fkey_label(const struct fkey *k) {
    switch (k->kind) {
    case FK_PAGE: return page_name[k->page];
    case FK_VIEW: return k->sub ? xen_view_name(k->sub - 1) : views_name(k->page, k->view);
    case FK_INST: return k->inst;
    }
    return "";
}
const char *fkey_short(const struct fkey *k) { return k->kind == FK_PAGE && page_short[k->page] ? page_short[k->page] : fkey_label(k); }
const char *fkey_about(const struct fkey *k) {
    switch (k->kind) {
    case FK_PAGE: return page_about[k->page];
    case FK_VIEW: return view_about(k);
    case FK_INST: { int i = find_inst(k->inst); return i < 0 ? "no instrument of that name on this stick" : insts[i].about[0] ? insts[i].about : "an instrument"; }
    }
    return "";
}

int fkey_inst(const struct fkey *k) { return find_inst(k->inst); }

int fkeys_for_page(int page) {
    for (int k = 0; k < FKEYS; k++) if (fkeys[k].kind == FK_PAGE && fkeys[k].page == page) return k;
    for (int k = 0; k < FKEYS; k++) if (fkey_page(&fkeys[k]) == page) return k;
    return -1;
}
const char *fkeys_page_key(int page) { int k = fkeys_for_page(page); return k < 0 ? 0 : fkey_names[k]; }
void fkeys_swap(int a, int b) { struct fkey t = fkeys[a]; fkeys[a] = fkeys[b]; fkeys[b] = t; }
bool fkeys_file_elsewhere(int key) {
    for (int k = 0; k < FKEYS; k++) if (k != key && fkey_page(&fkeys[k]) == PAGE_FILE) return true;
    return false;
}

/* ---- the places, in order: off, the pages, LINEAGE's views, XENAKIS's views, the instruments ---- */
#define VIEW_PLACES (LV_COUNT + XV_COUNT)
int fkeys_places(void) { return 1 + PAGE_COUNT + VIEW_PLACES + inst_count; }
static void view_place(int v, struct fkey *out) {                   /* v: LINEAGE's views, then XENAKIS's */
    *out = (struct fkey){ .kind = FK_VIEW, .page = PAGE_LINEAGE, .view = (uint8_t)(v < LV_COUNT ? v : LV_XEN),
                          .sub = (uint8_t)(v < LV_COUNT ? 0 : v - LV_COUNT + 1), .left_at = -1 };
}
void fkeys_place(int i, struct fkey *out) {
    *out = (struct fkey){ .kind = FK_OFF, .left_at = -1 };
    int v = i - 1 - PAGE_COUNT;
    if (i >= 1 && i <= PAGE_COUNT) { out->kind = FK_PAGE; out->page = (uint8_t)(i - 1); }
    else if (v >= 0 && v < VIEW_PLACES) view_place(v, out);
    else if (v >= VIEW_PLACES && i < fkeys_places()) { out->kind = FK_INST; snfmt(out->inst, sizeof out->inst, "%s", insts[v - VIEW_PLACES].name); }
}
int fkeys_place_of(const struct fkey *k) {
    switch (k->kind) {
    case FK_PAGE: return 1 + k->page;
    case FK_VIEW: return 1 + PAGE_COUNT + (k->sub ? LV_COUNT + k->sub - 1 : k->view);
    case FK_INST: { int i = find_inst(k->inst); return i < 0 ? 0 : 1 + PAGE_COUNT + VIEW_PLACES + i; }
    }
    return 0;
}

/* a name as a place: off, a page (or its short name), a view of LINEAGE or of XENAKIS (CLOUD and SIEVE too; XEN, the
   short name XENAKIS had as a page), an instrument */
static bool place_named(const char *s, int n, struct fkey *out) {
    *out = (struct fkey){ .kind = FK_OFF, .left_at = -1 };
    if (same_word(s, n, "off") || same_word(s, n, "none") || same_word(s, n, "-")) return true;
    for (int p = 0; p < PAGE_COUNT; p++)
        if (same_word(s, n, page_name[p]) || (page_short[p] && same_word(s, n, page_short[p]))) { out->kind = FK_PAGE; out->page = (uint8_t)p; return true; }
    if (same_word(s, n, "XEN")) { view_place(LV_XEN, out); return true; }
    for (int v = 0; v < VIEW_PLACES; v++) {
        const char *vn = v < LV_COUNT ? views_name(PAGE_LINEAGE, v) : xen_view_name(v - LV_COUNT); int vl = (int)strlen(vn);
        if (same_word(s, n, vn) || (n == vl - 1 && vn[n] == 'S' && same_prefix(s, n, vn))) { view_place(v, out); return true; }
    }
    for (int i = 0; i < inst_count; i++)
        if (same_word(s, n, insts[i].name)) { out->kind = FK_INST; snfmt(out->inst, sizeof out->inst, "%s", insts[i].name); return true; }
    return false;
}

/* FILE back on its own key when no key opens it (saving, and these keys, are there) */
static bool ensure_file(struct fkey k[FKEYS]) {
    for (int i = 0; i < FKEYS; i++) if (fkey_page(&k[i]) == PAGE_FILE) return false;
    k[PAGE_FILE] = (struct fkey){ .kind = FK_PAGE, .page = PAGE_FILE, .left_at = -1 };
    return true;
}

/* ---- KEYS.TXT: "F9 SHRUTI" a line; # to the end of a line is a comment; any case; a key not listed: its default ---- */
int fkeys_parse(const char *t, int len, struct fkey out[FKEYS], char *err, int cap) {
    fkeys_default(out);
    int bad = 0, line = 0;
    char why[80];
    if (cap) err[0] = 0;
    for (int p = 0; p < len; line++) {
        int e = p; while (e < len && t[e] != '\n') e++;
        int a = p, b = e; p = e + 1;
        for (int i = a; i < b; i++) if (t[i] == '#') { b = i; break; }
        while (a < b && (t[a] == ' ' || t[a] == '\t')) a++;
        while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\t' || t[b - 1] == '\r')) b--;
        if (a == b) continue;
        int k = a; while (k < b && t[k] != ' ' && t[k] != '\t' && t[k] != ':' && t[k] != '=') k++;
        int v = k; while (v < b && (t[v] == ' ' || t[v] == '\t' || t[v] == ':' || t[v] == '=')) v++;
        int key = -1;
        for (int i = 0; i < FKEYS; i++) if (same_word(t + a, k - a, fkey_names[i])) key = i;
        why[0] = 0;
        struct fkey f;
        if (key < 0) snfmt(why, sizeof why, "line %d: %s is not a key (F1 to F12)", line + 1, part(t + a, k - a, 12));
        else if (v == b) snfmt(why, sizeof why, "line %d: %s opens what? a name, or off", line + 1, fkey_names[key]);
        else if (!place_named(t + v, b - v, &f)) snfmt(why, sizeof why, "line %d: no page, view or instrument called %s", line + 1, part(t + v, b - v, 16));
        else out[key] = f;
        if (why[0]) { if (!bad++ && cap) snfmt(err, (size_t)cap, "%s", why); }
    }
    if (ensure_file(out)) { if (!bad++ && cap) snfmt(err, (size_t)cap, "FILE was on no key: it stays on F7"); }
    return bad;
}

static int add(char *o, int n, int cap, const char *s) { int l = (int)strlen(s); if (n + l < cap) { memcpy(o + n, s, (size_t)l); n += l; o[n] = 0; } return n; }
int fkeys_text(char *o, int cap) {
    int n = 0; char b[160];
    o[0] = 0;
    n = add(o, n, cap, "# BARE! - what each F key opens. Ctrl+1 to 9, 0, - and = are the same twelve keys.\r\n"
                       "# A line changes one key, like \"F9 SHRUTI\" or \"F10 off\". A key can open\r\n"
                       "#   a page:          PLAY SEQ WAVE STRETCH OPERATOR TAPE FILE MIX TOUCH FX LINEAGE\r\n"
                       "#   a LINEAGE view:  XENAKIS ANS REICH CARLOS RADIGUE MERZBOW\r\n"
                       "#   a XENAKIS view:  METASTASEIS CLOUDS SIEVES UPIC GENDY\r\n"
                       "#   an instrument:  ");
    for (int i = 0; i < inst_count; i++) { snfmt(b, sizeof b, " %s", insts[i].name); n = add(o, n, cap, b); }
    n = add(o, n, cap, "\r\n# A key without a line opens what it does by default:\r\n"
                       "#   F1 PLAY  F2 SEQ  F3 WAVE  F4 STRETCH  F5 OPERATOR  F6 TAPE\r\n"
                       "#   F7 FILE  F8 MIX  F9 TOUCH  F10 FX  F11 LINEAGE  F12 XENAKIS\r\n"
                       "# FILE is always on a key. BARE! writes this file again when its keys change\r\n"
                       "# (on the FILE page, Tab to KEYS; or a tab dragged along the top).\r\n\r\n");
    struct fkey d[FKEYS]; fkeys_default(d);
    int lines = 0;
    for (int k = 0; k < FKEYS; k++) {
        if (fkey_same(&fkeys[k], &d[k])) continue;
        snfmt(b, sizeof b, "%s%s%s\r\n", fkey_names[k], k < 9 ? "  " : " ", fkeys[k].kind == FK_OFF ? "off" : fkey_label(&fkeys[k]));
        n = add(o, n, cap, b); lines++;
    }
    if (!lines) n = add(o, n, cap, "# (every key opens what it does by default)\r\n");
    return n;
}

void fkeys_load(void) {
    struct fat_file f;
    fkeys_trouble = false;
    if (!disk.have_boot_fat) { snfmt(fkeys_status, sizeof fkeys_status, "no stick: changes last until the machine is turned off"); return; }
    if (!fat_find(&disk.fat, "KEYS.TXT", &f)) { snfmt(fkeys_status, sizeof fkeys_status, "no KEYS.TXT on the stick: a change writes one"); return; }
    static char buf[4096];
    uint32_t len = MIN(f.size, (uint32_t)sizeof buf);
    if (!fat_read(&disk.fat, &f, 0, buf, len)) { snfmt(fkeys_status, sizeof fkeys_status, "KEYS.TXT could not be read"); fkeys_trouble = true; return; }
    struct fkey k[FKEYS]; char err[80];
    int bad = fkeys_parse(buf, (int)len, k, err, sizeof err);
    for (int i = 0; i < FKEYS; i++) k[i].left_at = fkey_same(&k[i], &fkeys[i]) ? fkeys[i].left_at : -1;   /* kept where unchanged */
    memcpy(fkeys, k, sizeof k);
    fkeys_trouble = bad > 0;
    if (bad) snfmt(fkeys_status, sizeof fkeys_status, "KEYS.TXT: %d mistake%s, %s", bad, bad > 1 ? "s" : "", err);
    else snfmt(fkeys_status, sizeof fkeys_status, "from KEYS.TXT on the stick");
    logf("keys: %s", fkeys_status);
}

/* ---- the write, a moment after the last change (a few in a row are one write), never during an export or install ---- */
static bool pending; static uint64_t changed_ms;
void fkeys_changed(uint64_t now) { pending = true; changed_ms = now; }
bool fkeys_pending(void) { return pending; }
void fkeys_work(uint64_t now) {
    if (!pending || now - changed_ms < 800 || song.exporting || inst.running) return;
    pending = false;
    fkeys_trouble = false;
    if (!disk.have_boot_fat) { snfmt(fkeys_status, sizeof fkeys_status, "no stick: changes last until the machine is turned off"); return; }
    static char text[2048];
    int n = fkeys_text(text, sizeof text);
    struct fat_file f;
    bool ok = fat_create(&disk.fat, "", "KEYS.TXT", (uint32_t)n, &f) && fat_write_inplace(&disk.fat, &f, 0, text, (uint32_t)n);
    snfmt(fkeys_status, sizeof fkeys_status, ok ? "kept in KEYS.TXT on the stick" : "KEYS.TXT could not be written");
    fkeys_trouble = !ok;
    logf("keys: %s", fkeys_status);
}
