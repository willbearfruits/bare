#pragma once
/* METASTASEIS (a XENAKIS view, after Iannis Xenakis's Metastaseis, 1953-54): families of string glissandi strung
   between two guide lines on a page of time and pitch. Xenakis drew each of his 46 strings' glissandi as a straight
   line on graph paper; lines joining two lines, point by point, make a curved envelope none of them draws — a ruled
   surface, the geometry of the Philips Pavilion. Here a family is two guides and up to 46 strings between them,
   evenly spaced or in the golden section's proportions (Le Corbusier's Modulor), straight across or crossed; a cursor
   plays the page and every string sounds its glissando as it passes (into the UPIC channel). */
#include <stdint.h>
#include <stdbool.h>

#define META_FAMILIES 4
#define META_MAX      46                  /* strings in a family: Metastaseis's 46 */
#define META_STRINGS  (META_FAMILIES * META_MAX)
#define META_VOICES   64                  /* strings sounding at once, at most */
enum { META_VLN1, META_VLN2, META_VLA, META_VC, META_CB, META_SPLIT, META_SECTIONS };   /* SPLIT: all five, as the score */

struct meta_line { uint16_t t0, p0, t1, p1; };   /* time across the page 0..65535, pitch in 1/256 semitones */
struct meta_family {
    struct meta_line a, b;                /* the guides: string i goes from a at u_i to b at u_i (or 1 - u_i, crossed) */
    uint8_t n;                            /* strings, 2 .. 46 */
    bool    cross, modulor, on;
    uint8_t section;                      /* META_VLN1 … META_CB, or META_SPLIT */
    uint8_t level;                        /* 0 .. 100 */
};
struct meta_string { uint16_t t0, t1, p0, p1; uint8_t fam, section; };
struct meta_state {
    struct meta_family fam[META_FAMILIES];
    uint8_t  bars, seconds;               /* the page's length: bars at the tempo, else seconds */
    bool     playing;
    volatile uint32_t pos;                /* the cursor: 0 .. 65535 across the page, << 16 */
    uint32_t changes;                     /* compiles: the picture keys on it */
};
extern struct meta_state meta;
extern const char *const meta_section_names[META_SECTIONS];

void meta_init(uint32_t rate);
void meta_defaults(void);                 /* the opening's fan, and a crossing */
void meta_compile(void);                  /* the strings from the families (main loop, after any change) */
int  meta_strings(const struct meta_string **out);   /* the strings now */
int  meta_section_of(const struct meta_string *s, int index_in_family, int n);
bool meta_render(int32_t *l, int32_t *r, uint32_t n, bool add);   /* audio side: into the UPIC bus; false: nothing */
int  meta_write_upic(int family);         /* a family's strings onto UPIC's page as arcs (-1: every family on):
                                             how many, -1 when there is no room */
