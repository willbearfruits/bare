#pragma once
/* The XENAKIS page's views (core/page_xen.c holds the page): each is a file, core/page_<view>.c, with the hooks of a
   page, drawn below the page's view bar. */
#include "ui.h"

struct xen_view {
    const char *name;
    bool (*key_event)(uint8_t code, bool down, uint64_t now);
    bool (*typing)(void);                                 /* optional */
    void (*pointer)(uint64_t now);                        /* optional: the pointer, and the touchpad's fingers (the page owns the pad) */
    void (*draw)(uint64_t now);
    bool (*midi)(uint8_t note, uint8_t vel, uint64_t now);   /* optional */
};
extern const struct xen_view xen_upic, xen_gendy, xen_cloud, xen_sieve;
enum { XV_UPIC, XV_GENDY, XV_CLOUDS, XV_SIEVES, XV_COUNT };
int  xen_current(void);                                   /* the view showing */
void xen_goto(int view);

/* a line of a view's picture: thicker on 4K screens, where the text is doubled */
void xen_line(int x0, int y0, int x1, int y1, uint8_t c);
/* the letter rows as a piano, two octaves from C of `octave`: the note, or -1 */
int xen_key_note(uint8_t code, int octave);
