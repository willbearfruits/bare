/* OPERATOR: 4-operator FM, Ableton-Operator style. Four patches, used anywhere as the sounds FM 1–4. */
#include "ui.h"
#include "gfx.h"
#include "fm.h"
#include "synth.h"
#include "keys.h"
#include "fkeys.h"
#include "undo.h"

enum { FP_WAVE, FP_RATIO, FP_FINE, FP_LEVEL, FP_A, FP_D, FP_S, FP_R, FP_VEL, FP_KEY, FP_COUNT };
static const char *const param_names[FP_COUNT] = { "WAVE", "RATIO", "FINE", "LEVEL", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "VELOCITY", "KEY SCALE" };
static const char *const wave_names[4] = { "sine", "tri", "saw", "square" };
/* the patch's LFO: the rows after the operators' */
enum { LP_RATE = FP_COUNT, LP_WAVE, LP_PITCH, LP_AMP, LP_MOD, LP_END };
static const char *const lfo_names[5] = { "RATE", "WAVE", "PITCH", "AMP", "MOD" };
static const char *const lfo_waves[LFO_WAVES] = { "sine", "tri", "saw", "square", "random" };

static int patch, op, param = FP_LEVEL;
static bool naming; static char name[12]; static int name_len;
static struct rect grid_px, lfo_px; static int colw_px, bar_off_px, bar_w_px;   /* for the pointer */
static bool dragging;

static int pmax(int p) {
    switch (p) { case FP_WAVE: return 3; case FP_RATIO: return 32; case FP_FINE: return 99; case FP_LEVEL: return 100;
                 case FP_A: return 2000; case FP_D: return 4000; case FP_S: return 100; case FP_R: return 4000;
                 case LP_WAVE: return LFO_WAVES - 1; default: return 100; }
}
static uint8_t *lfo_field(int p) {
    struct fm_patch *pt = &fm_bank[patch];
    return p == LP_RATE ? &pt->lfo_rate : p == LP_WAVE ? &pt->lfo_wave : p == LP_PITCH ? &pt->lfo_pitch : p == LP_AMP ? &pt->lfo_amp : &pt->lfo_mod;
}
static int pget(int o, int p) {
    if (p >= FP_COUNT) return *lfo_field(p);
    const struct fm_op *x = &fm_bank[patch].op[o];
    switch (p) { case FP_WAVE: return x->wave; case FP_RATIO: return x->ratio_x2; case FP_FINE: return x->fine; case FP_LEVEL: return x->level;
                 case FP_A: return x->a_ms; case FP_D: return x->d_ms; case FP_S: return x->s_pct; case FP_R: return x->r_ms;
                 case FP_VEL: return x->vel; default: return x->key; }
}
static void pset(int o, int p, int v) {
    v = CLAMP(v, p == FP_RATIO ? 1 : 0, pmax(p));
    if (p >= FP_COUNT) { *lfo_field(p) = (uint8_t)v; return; }
    struct fm_op *x = &fm_bank[patch].op[o];
    switch (p) { case FP_WAVE: x->wave = (uint8_t)v; break; case FP_RATIO: x->ratio_x2 = (uint8_t)v; break; case FP_FINE: x->fine = (uint8_t)v; break;
                 case FP_LEVEL: x->level = (uint8_t)v; break; case FP_A: x->a_ms = (uint16_t)v; break; case FP_D: x->d_ms = (uint16_t)v; break;
                 case FP_S: x->s_pct = (uint8_t)v; break; case FP_R: x->r_ms = (uint16_t)v; break;
                 case FP_VEL: x->vel = (uint8_t)v; break; default: x->key = (uint8_t)v; break; }
}
static void ptext(int o, int p, char *out, int cap) {
    int v = pget(o, p);
    switch (p) {
    case FP_WAVE: snfmt(out, cap, "%s", wave_names[v & 3]); break;
    case FP_RATIO: snfmt(out, cap, "%d.%d", v / 2, (v % 2) * 5); break;
    case FP_FINE: snfmt(out, cap, "+.%02d", v); break;
    case FP_A: case FP_D: case FP_R: snfmt(out, cap, "%d ms", v); break;
    case LP_WAVE: snfmt(out, cap, "%s", lfo_waves[v % LFO_WAVES]); break;
    case LP_RATE: { uint32_t mhz = 100; for (int i = 0; i < v / 13; i++) mhz *= 2; mhz = mhz * (1000 + (uint32_t)(v % 13) * 1000 / 13 * 693 / 1000) / 1000;
                    snfmt(out, cap, "%u.%u Hz", mhz / 1000, mhz % 1000 / 100); break; }
    default: snfmt(out, cap, "%d%%", v); break;
    }
}
static void adjust(int dir, bool big) {
    int v = pget(op, param), step;
    if (param == FP_A || param == FP_D || param == FP_R) step = big ? MAX(10, v / 4) : MAX(1, v / 20);
    else step = big ? 10 : param == LP_WAVE ? 1 : param >= FP_COUNT || param >= FP_VEL ? 5 : 1;
    pset(op, param, v + dir * step);
}

static bool typing(void) { return naming; }

static bool key(uint8_t code, bool down, uint64_t now) {
    if (!down) return naming;
    struct fm_patch *pt = &fm_bank[patch];
    if (naming) {
        if (code == KEY_ENTER) { naming = false; if (name_len) snfmt(pt->name, sizeof pt->name, "%s", name); }
        else if (code == KEY_ESC) naming = false;
        else if (code == KEY_BACKSPACE) { if (name_len) name[--name_len] = 0; }
        else if (code >= ' ' && code < 0x7F && name_len < (int)sizeof name - 1) { char c = (char)code; if (c >= 'a' && c <= 'z') c -= 32; name[name_len++] = c; name[name_len] = 0; }
        return true;
    }
    if (code == KEY_LEFT || code == KEY_RIGHT || code == '[' || code == ']' || code == KEY_PGUP || code == KEY_PGDN || code == '-' || code == '=' || code == KEY_ENTER) {
        char w[24]; snfmt(w, sizeof w, "patch %d", patch + 1); undo_one(U_FM, patch, w, now);
    }
    switch (code) {
    case KEY_TAB:   op = (op + 1) % FM_OPS; return true;
    case KEY_UP:    param = (param + LP_END - 1) % LP_END; return true;
    case KEY_DOWN:  param = (param + 1) % LP_END; return true;
    case KEY_LEFT:  adjust(-1, false); return true;
    case KEY_RIGHT: adjust(1, false); return true;
    case '[': adjust(-1, true); return true;
    case ']': adjust(1, true); return true;
    case KEY_PGUP:  pt->algo = (uint8_t)((pt->algo + FM_ALGOS - 1) % FM_ALGOS); return true;
    case KEY_PGDN:  pt->algo = (uint8_t)((pt->algo + 1) % FM_ALGOS); return true;
    case '-': pt->feedback = (uint8_t)(pt->feedback >= 5 ? pt->feedback - 5 : 0); return true;
    case '=': pt->feedback = (uint8_t)(pt->feedback <= 95 ? pt->feedback + 5 : 100); return true;
    case KEY_HOME:  patch = (patch + FM_PATCHES - 1) % FM_PATCHES; return true;
    case KEY_END:   patch = (patch + 1) % FM_PATCHES; return true;
    case KEY_ENTER: naming = true; name_len = 0; name[0] = 0; return true;
    }
    return false;
}

/* press on a parameter to select it; drag across its bar to set it */
static void pointer(uint64_t now) {
    if (!ptr.down) { dragging = false; return; }
    const struct font *f = text_font();
    if (ptr.pressed) {
        if (ui_in(lfo_px, ptr.x, ptr.y)) { param = LP_RATE + CLAMP((ptr.y - lfo_px.y) / f->height, 0, 4); dragging = true; }
        else if (ui_in(grid_px, ptr.x, ptr.y) && colw_px) {
            op = CLAMP((ptr.x - grid_px.x) / colw_px, 0, FM_OPS - 1);
            param = CLAMP((ptr.y - grid_px.y) / f->height, 0, FP_COUNT - 1);
            dragging = true;
        } else return;
    }
    if (!dragging) return;
    { char w[24]; snfmt(w, sizeof w, "patch %d", patch + 1); undo_one(U_FM, patch, w, now); }
    if (param >= FP_COUNT) {                                /* an LFO bar: the right half of its row */
        int bx = lfo_px.x + lfo_px.w / 2, bw = lfo_px.w / 2;
        pset(op, param, (int)((int64_t)CLAMP(ptr.x - bx, 0, bw) * pmax(param) / MAX(1, bw)));
        return;
    }
    if (bar_w_px < 8) return;
    int bx = grid_px.x + op * colw_px + bar_off_px, lo = param == FP_RATIO ? 1 : 0;
    int frac = CLAMP(ptr.x - bx, 0, bar_w_px);
    pset(op, param, lo + (int)((int64_t)frac * (pmax(param) - lo) / bar_w_px));
}

/* ---- the algorithm, drawn from the routing table the engine uses ---- */
static const uint8_t layout[FM_ALGOS][FM_OPS][2] = {           /* column, row of OP1..OP4 in a 4 × 4 grid */
    { { 1, 3 }, { 1, 2 }, { 1, 1 }, { 1, 0 } },                 /* serial */
    { { 1, 2 }, { 0, 1 }, { 2, 1 }, { 2, 0 } },                 /* Y */
    { { 1, 2 }, { 0, 1 }, { 2, 1 }, { 0, 0 } },                 /* two pair */
    { { 1, 2 }, { 0, 1 }, { 1, 1 }, { 2, 1 } },                 /* fan in */
    { { 0, 2 }, { 0, 1 }, { 2, 2 }, { 2, 1 } },                 /* 2+2 */
    { { 0, 2 }, { 2, 2 }, { 2, 1 }, { 2, 0 } },                 /* 3+1 */
    { { 0, 2 }, { 1, 2 }, { 2, 2 }, { 1, 1 } },                 /* 1 to 3 */
    { { 0, 2 }, { 1, 2 }, { 2, 2 }, { 3, 2 } },                 /* additive */
};
static void algo_view(struct rect r, const struct fm_patch *pt) {
    const struct fm_algo *a = &fm_algos[pt->algo];
    const struct font *f = text_font();
    int maxc = 0, maxr = 0;
    for (int i = 0; i < FM_OPS; i++) { maxc = MAX(maxc, layout[pt->algo][i][0]); maxr = MAX(maxr, layout[pt->algo][i][1]); }
    int cw = r.w / 4, rh = MIN((r.h - 7) / (maxr + 2), f->height * 2), bw = MIN(cw - 6, f->width * 3 + 6), bh = f->height + 2;
    int ox = r.x + (r.w - (maxc + 1) * cw) / 2;
    int bx[FM_OPS], by[FM_OPS];
    for (int i = 0; i < FM_OPS; i++) { bx[i] = ox + layout[pt->algo][i][0] * cw + (cw - bw) / 2; by[i] = r.y + 7 + layout[pt->algo][i][1] * rh; }   /* room above for the feedback loop */
    /* modulation arrows */
    for (int t = 0; t < FM_OPS; t++)
        for (int m = t + 1; m < FM_OPS; m++) {
            if (!(a->mod_mask[t] & (1 << m))) continue;
            int x0 = bx[m] + bw / 2, y0 = by[m] + bh, x1 = bx[t] + bw / 2, y1 = by[t] - 1;
            uint8_t c = pt->op[m].level ? ramp(R_CYAN, 10) : ramp(R_PANEL, 7);
            gfx_line(x0, y0, x1, y1, c);
            gfx_line(x1, y1, x1 - 3, y1 - 3, c); gfx_line(x1, y1, x1 + 3, y1 - 3, c);
        }
    /* carriers into the output bus */
    int bus = r.y + 7 + (maxr + 1) * rh - rh / 3, cx0 = 1 << 30, cx1 = -1;
    for (int i = 0; i < FM_OPS; i++) {
        if (!(a->carriers & (1 << i))) continue;
        int cx = bx[i] + bw / 2;
        gfx_vline(cx, by[i] + bh, bus - by[i] - bh, ramp(R_AMBER, 9));
        cx0 = MIN(cx0, cx); cx1 = MAX(cx1, cx);
    }
    gfx_hline(cx0 - 4, bus, cx1 - cx0 + 9, ramp(R_AMBER, 9));
    gfx_text((cx0 + cx1) / 2 - f->width * 3 / 2, bus + 3, "OUT", f, C_DIM, -1, 1);
    /* feedback loop on OP4 */
    if (pt->feedback) {
        int x0 = bx[3] + bw, ym = by[3] + bh / 2, top = by[3] - 4;
        uint8_t c = ramp(R_PINK, 4 + pt->feedback / 10);
        gfx_hline(x0, ym, 5, c); gfx_vline(x0 + 5, top, ym - top, c); gfx_hline(bx[3] + bw / 2, top, x0 + 5 - bx[3] - bw / 2, c);
        gfx_vline(bx[3] + bw / 2, top, 3, c);
    }
    /* the operator boxes on top */
    for (int i = 0; i < FM_OPS; i++) {
        bool car = a->carriers & (1 << i), sel = i == op, silent = !pt->op[i].level;
        uint8_t fill = silent ? ramp(R_PANEL, 3) : car ? C_AMBER_D : ramp(R_CYAN, 3);
        int outline = sel ? C_BRIGHT : silent ? ramp(R_PANEL, 7) : car ? ramp(R_AMBER, 11) : ramp(R_CYAN, 10);
        gfx_round(bx[i], by[i], bw, bh, 4, fill, outline);
        char n[2] = { (char)('1' + i), 0 };
        gfx_text(bx[i] + (bw - f->width) / 2, by[i] + 1, n, f, silent ? C_DIM : C_BRIGHT, -1, 1);
    }
}

/* ADSR sketch: widths follow the times (square-root scaled so short and long both read), filled */
static void env_view(struct rect r, const struct fm_op *o, int rp) {
    int sq[3] = { 0, 0, 0 }, t[3] = { o->a_ms, o->d_ms, o->r_ms };
    for (int i = 0; i < 3; i++) { int s = 0; while ((s + 1) * (s + 1) <= t[i]) s++; sq[i] = s + 2; }   /* √ms */
    int hold = MAX(r.w / 6, 4), total = sq[0] + sq[1] + sq[2], avail = r.w - hold - 2;
    int wa = sq[0] * avail / total, wd = sq[1] * avail / total, wr = avail - wa - wd;
    int bot = r.y + r.h - 1, top = r.y + 1, sus = bot - (bot - top) * o->s_pct / 100;
    int px[5] = { r.x, r.x + wa, r.x + wa + wd, r.x + wa + wd + hold, r.x + wa + wd + hold + wr };
    int py[5] = { bot, top, sus, sus, bot };
    for (int s = 0; s < 4; s++)
        for (int x = px[s]; x <= px[s + 1]; x++) {
            int y = px[s + 1] == px[s] ? py[s + 1] : py[s] + (py[s + 1] - py[s]) * (x - px[s]) / (px[s + 1] - px[s]);
            gfx_vline(x, y, bot - y + 1, ramp(rp, 3));
        }
    for (int s = 0; s < 4; s++) gfx_line(px[s], py[s], px[s + 1], py[s + 1], ramp(rp, 13));
}

static void draw(uint64_t now) {
    (void)now;
    int cols = text_cols(), rows = text_rows();
    struct fm_patch *pt = &fm_bank[patch];
    const struct fm_algo *al = &fm_algos[pt->algo];
    int x = 2, y = 2, w = cols - 4, h = 24;
    char buf[64];
    snfmt(buf, sizeof buf, "OPERATOR · patch %d of %d · %s", patch + 1, FM_PATCHES, pt->name);
    ui_panel(x, y, w, h, buf, C_AMBER);
    /* left: algorithm, feedback, patch keys */
    snfmt(buf, sizeof buf, "%d  %s", pt->algo + 1, al->name);
    text_str(x + 2, y + 1, "ALGORITHM", C_DIM, C_PANEL);
    text_str(x + 2, y + 2, buf, C_CYAN, C_PANEL);
    uint32_t ak = ui_hash_int(UI_HASH0, pt->algo | pt->feedback << 8 | op << 16);
    for (int o = 0; o < FM_OPS; o++) ak = ui_hash_int(ak, pt->op[o].level != 0);
    struct rect ac;
    if (ui_canvas_keyed(&ac, x + 1, y + 3, 19, 8, C_PANEL, ak)) algo_view(ac, pt);
    snfmt(buf, sizeof buf, "%d%%", pt->feedback);
    ui_label(x + 2, y + 12, "FEEDBACK", buf, C_PINK, C_PANEL);
    ui_bar(x + 2, y + 13, 16, pt->feedback, 100, C_PINK, C_PANEL);
    /* the LFO */
    text_str(x + 2, y + 15, "LFO", C_DIM, C_PANEL);
    lfo_px = text_rect(x + 2, y + 16, 18, 5);
    for (int i = 0; i < 5; i++) {
        int pr = LP_RATE + i, ry = y + 16 + i; bool sel = param == pr;
        uint8_t bg = sel ? C_BORDER : C_PANEL;
        text_fill(x + 1, ry, 19, 1, ' ', C_TEXT, bg);
        text_str(x + 2, ry, lfo_names[i], sel ? C_GREEN : C_DIM, bg);
        ptext(op, pr, buf, sizeof buf);
        if (pr == LP_WAVE || pr == LP_RATE) text_str(x + 8, ry, buf, sel ? C_BRIGHT : C_TEXT, bg);
        else ui_bar(x + 11, ry, 9, pget(op, pr), 100, C_GREEN, bg);
    }
    if (naming) { snfmt(buf, sizeof buf, "name: %s_", name); text_str(x + 2, y + h - 2, buf, C_BRIGHT, C_PANEL); }
    /* operator columns */
    int gx = x + 22, colw = MIN(32, (w - 24) / FM_OPS);
    const struct font *f = text_font();
    grid_px = text_rect(gx, y + 3, colw * FM_OPS, FP_COUNT);
    colw_px = colw * f->width; bar_off_px = 19 * f->width; bar_w_px = (colw - 21) * f->width;
    for (int o = 0; o < FM_OPS; o++) {
        int cx = gx + o * colw;
        bool car = al->carriers & (1 << o), sel_op = o == op;
        uint8_t hb = sel_op ? (car ? C_AMBER : C_CYAN) : C_BORDER;
        text_fill(cx, y + 1, colw - 1, 1, ' ', C_BLACK, hb);
        snfmt(buf, sizeof buf, "OP%d  %s", o + 1, car ? "carrier" : "modulator");
        text_str(cx + 1, y + 1, buf, sel_op ? C_BLACK : car ? C_AMBER : C_CYAN, hb);
        for (int pr = 0; pr < FP_COUNT; pr++) {
            int ry = y + 3 + pr; bool sel = sel_op && pr == param;
            uint8_t bg = sel ? C_BORDER : C_PANEL;
            text_fill(cx, ry, colw - 1, 1, ' ', C_TEXT, bg);
            text_str(cx + 1, ry, param_names[pr], sel ? (car ? C_AMBER : C_CYAN) : C_DIM, bg);
            ptext(o, pr, buf, sizeof buf);
            text_str(cx + 11, ry, buf, sel ? C_BRIGHT : C_TEXT, bg);
            if (colw >= 26) ui_bar(cx + 19, ry, colw - 21, pget(o, pr) - (pr == FP_RATIO), pmax(pr) - (pr == FP_RATIO), car ? C_AMBER : C_CYAN, bg);
        }
        text_str(cx + 1, y + 14, "ENVELOPE", C_DIM, C_PANEL);
        struct rect ec;
        if (ui_canvas_keyed(&ec, cx + 1, y + 15, colw - 3, 4, C_PANEL, ui_hash_int(ui_hash(UI_HASH0, &pt->op[o], sizeof pt->op[o]), car)))
            env_view(ec, &pt->op[o], car ? R_AMBER : R_CYAN);
        snfmt(buf, sizeof buf, "%s ×%d.%02d", wave_names[pt->op[o].wave & 3], pt->op[o].ratio_x2 / 2, (pt->op[o].ratio_x2 % 2) * 50 + pt->op[o].fine);
        text_str(cx + 1, y + 20, buf, C_DIM, C_PANEL);
    }
    LEGEND(gx, y + 22, w - 24, 1, C_PANEL, "TAB", "operator", "↑ ↓", "parameter", "← → [ ]", "adjust", "pointer", "drag a bar",
           "PGUP PGDN", "algo", "- =", "feedback", "HOME END", "patch", "ENTER", "rename");
    int sy = y + h + 1, sh = rows - sy - 2;
    if (sh >= 5) {
        ui_panel(x, sy, w, sh, "SCOPE", C_SCOPE);
        ui_scope(x + 1, sy + 1, w - 2, sh - 2);
    }
    FOOTER("A-' Z-/", "play this patch", fkeys_page_key(PAGE_PLAY), "play page", "FM 1-4", "on PLAY (⇧ arrows) and sequencer tracks (PGUP PGDN)");
}

static int strum_sound(void) { return P_FM1 + patch; }

const struct page page_fm = { "OPERATOR", true, key, typing, pointer, strum_sound, draw, false, 0, "FM" };
