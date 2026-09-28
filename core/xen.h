#pragma once
/* XENAKIS, a lineage inside LINEAGE (core/page_xen.c holds its set): Iannis Xenakis's ways of making music, each a view
   of its own, core/page_<view>.c (see views.h). Their bar is row 2, under LINEAGE's; the views start below it. */
#include "views.h"

#define XEN_TOP 3                                         /* the first row under both bars */
extern const struct view xen_meta, xen_upic, xen_gendy, xen_cloud, xen_sieve;
extern const struct view lin_xen;                         /* XENAKIS, as LINEAGE's view: this set */
enum { XV_META, XV_CLOUDS, XV_SIEVES, XV_UPIC, XV_GENDY, XV_COUNT };   /* in the order of their music */
const struct viewset *xen_views(void);
int  xen_current(void);                                   /* the view showing (when XENAKIS shows) */
const char *xen_view_name(int view);
void xen_goto(int view);
void xen_again(void);                                     /* the next view: its key pressed again */

/* a line of a view's picture: thicker on 4K screens, where the text is doubled */
void xen_line(int x0, int y0, int x1, int y1, uint8_t c);
/* the letter rows as a piano, two octaves from C of `octave`: the note, or -1 */
int xen_key_note(uint8_t code, int octave);
