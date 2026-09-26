/* Sieves; see sieve.h. The text is read by recursive descent (| binds loosest, then &, then - and parentheses) into a
   postfix program: a class tests x, NOT, AND and OR work a small stack of truths. */
#include "sieve.h"
#include "platform.h"
#include "libc.h"

struct sieve sieves[SIEVES];
volatile uint32_t sieve_changes;

const struct sieve_example sieve_examples[] = {
    { "8@4", "beats 2 and 4" },
    { "5@0|8@3", "a lopsided kick: 40 steps before it repeats" },
    { "2@0|3@1", "busy, turning every 6" },
    { "3@0|4@0", "three against four" },
    { "13@0|13@3|13@5|13@6|13@9|13@11", "13 steps, unevenly: no bar holds it" },
    { "-(3@0|5@0)", "every number neither 3 nor 5 divides" },
    { "(-3@2&4@0)|(-3@1&4@1)|(3@2&4@2)|(-3@0&4@3)", "the major scale, as a sieve" },
    { "3@0|3@1", "half, whole, half: the octatonic scale" },
    { "2@0", "the whole-tone scale" },
    { "11@0|11@2|11@5|11@7", "a scale that repeats every 11: no octaves" },
    { "(12@0|12@3|12@7)&-(24@12|24@15|24@19)", "a chord every other octave" },
};
const int sieve_example_count = (int)ARRAY_LEN(sieve_examples);

static const char *const defaults[SIEVES] = { "5@0|8@3", "8@4", "2@0|3@1", "7@0|16@8" };

/* ---- the reader ---- */
struct rd { const char *p; struct sieve_prog *out; const char *err; uint32_t lcm; };
static void emit(struct rd *r, uint8_t op, uint16_t m, uint16_t rr) {
    if (r->err) return;
    if (r->out->n >= SIEVE_OPS) { r->err = "too long"; return; }
    r->out->op[r->out->n].op = op; r->out->op[r->out->n].m = m; r->out->op[r->out->n].r = rr; r->out->n++;
}
static void blank(struct rd *r) { while (*r->p == ' ') r->p++; }
static bool number(struct rd *r, uint32_t *v) {
    blank(r);
    if (*r->p < '0' || *r->p > '9') return false;
    uint32_t n = 0;
    while (*r->p >= '0' && *r->p <= '9') { n = n * 10 + (uint32_t)(*r->p++ - '0'); if (n > 60000) { r->err = "a number past 60000"; return false; } }
    *v = n; return true;
}
static uint32_t gcd(uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }
static void expr(struct rd *r);
static void factor(struct rd *r) {
    blank(r);
    if (r->err) return;
    if (*r->p == '-') { r->p++; factor(r); emit(r, SV_NOT, 0, 0); return; }
    if (*r->p == '(') {
        r->p++; expr(r); blank(r);
        if (r->err) return;
        if (*r->p != ')') { r->err = "a ( without its )"; return; }
        r->p++; return;
    }
    uint32_t m, rr = 0;
    if (!number(r, &m)) { if (!r->err) r->err = *r->p ? "expected a number" : "it ends too soon"; return; }
    blank(r);
    if (*r->p == '@') { r->p++; if (!number(r, &rr)) { if (!r->err) r->err = "expected a number after @"; return; } }
    if (!m) { r->err = "0 is no modulus"; return; }
    emit(r, SV_RES, (uint16_t)m, (uint16_t)(rr % m));
    if (r->lcm) { uint64_t l = (uint64_t)r->lcm / gcd(r->lcm, m) * m; r->lcm = l > 100000 ? 0 : (uint32_t)l; }
}
static void term(struct rd *r) {
    factor(r);
    for (blank(r); !r->err && *r->p == '&'; blank(r)) { r->p++; factor(r); emit(r, SV_AND, 0, 0); }
}
static void expr(struct rd *r) {
    term(r);
    for (blank(r); !r->err && *r->p == '|'; blank(r)) { r->p++; term(r); emit(r, SV_OR, 0, 0); }
}

static bool run(const struct sieve_prog *pg, int32_t x) {
    bool st[SIEVE_OPS]; int sp = 0;
    for (int i = 0; i < pg->n; i++) {
        switch (pg->op[i].op) {
        case SV_RES: { int32_t m = pg->op[i].m, v = x % m; if (v < 0) v += m; st[sp++] = v == pg->op[i].r; break; }
        case SV_NOT: st[sp - 1] = !st[sp - 1]; break;
        case SV_AND: sp--; st[sp - 1] = st[sp - 1] && st[sp]; break;
        default:     sp--; st[sp - 1] = st[sp - 1] || st[sp]; break;
        }
    }
    return sp == 1 && st[0];
}

bool sieve_compile(struct sieve *s) {
    struct sieve_prog pg; memset(&pg, 0, sizeof pg);
    s->text[SIEVE_TEXT - 1] = 0;
    struct rd r = { s->text, &pg, 0, 1 };
    expr(&r); blank(&r);
    if (!r.err && *r.p) r.err = *r.p == ')' ? "a ) without its (" : "something it can't read";
    if (!r.err && !pg.n) r.err = "empty";
    if (r.err) { snfmt(s->err, sizeof s->err, "%s (at %d)", r.err, (int)(r.p - s->text) + 1); return false; }
    uint32_t st = plat_irq_save();
    s->prog = pg; s->ok = true;
    plat_irq_restore(st);
    s->period = r.lcm; s->err[0] = 0;
    s->members = 0;
    uint32_t span = s->period ? s->period : 1000;
    for (uint32_t x = 0; x < span; x++) s->members += run(&pg, (int32_t)x);
    sieve_changes++;
    return true;
}

bool sieve_has(const struct sieve *s, int32_t x) { return s->ok && run(&s->prog, x); }

int32_t sieve_next(const struct sieve *s, int32_t x, int within) {
    if (!s->ok || !s->members) return SIEVE_NONE;
    for (int i = 0; i <= within; i++) if (run(&s->prog, x + i)) return x + i;
    return SIEVE_NONE;
}
int32_t sieve_nearest(const struct sieve *s, int32_t x, int within) {
    if (!s->ok || !s->members) return SIEVE_NONE;
    for (int i = 0; i <= within; i++) {
        if (run(&s->prog, x - i)) return x - i;
        if (run(&s->prog, x + i)) return x + i;
    }
    return SIEVE_NONE;
}
int32_t sieve_snap_pitch(const struct sieve *s, int32_t q8) {
    int u = s->unit ? s->unit : 1;
    int32_t x = (q8 * u + 128) >> 8;                                  /* the nearest step of the unit */
    int32_t m = sieve_nearest(s, x, 24 * u);
    return m == SIEVE_NONE ? q8 : m * 256 / u;
}

void sieve_init(void) {
    for (int i = 0; i < SIEVES; i++) {
        memset(&sieves[i], 0, sizeof sieves[i]);
        snfmt(sieves[i].text, SIEVE_TEXT, "%s", defaults[i]);
        sieves[i].unit = 1;
        sieve_compile(&sieves[i]);
    }
}
