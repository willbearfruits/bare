#pragma once
/* Pages made of views: LINEAGE (core/page_lineage.c), whose XENAKIS view is itself a set of views (core/page_xen.c).
   A view is a file with the hooks of a page, drawn below its set's bar: LINEAGE's on row 1, XENAKIS's on row 2 under
   it. A bar names the views, each with the year of the music it comes from; a click picks one, and the key that
   opened them pressed again steps to the next. */
#include "ui.h"

struct view {
    const char *name;
    bool (*key_event)(uint8_t code, bool down, uint64_t now);
    bool (*typing)(void);                                 /* optional */
    void (*pointer)(uint64_t now);                        /* optional: the pointer, and the touchpad's fingers (the page owns the pad) */
    void (*draw)(uint64_t now);
    bool (*midi)(uint8_t note, uint8_t vel, uint64_t now);   /* optional */
    const char *year;                                     /* optional: after the name in the bar */
};

#define VIEWS_MAX 8
struct viewset {
    const struct view *const *v;                          /* the views, in the bar's order */
    int n, cur;                                           /* how many; the one showing */
    int row;                                              /* the bar's row: 1 under the title bar, 2 inside a view */
    int (*step_key)(void);                                /* optional: the F key that steps it (the bar's hint), or -1 */
    int bar_x[VIEWS_MAX + 1];                             /* cells, as last drawn */
};

bool views_key(struct viewset *s, uint8_t code, bool down, uint64_t now);
bool views_typing(const struct viewset *s);
bool views_midi(struct viewset *s, uint8_t note, uint8_t vel, uint64_t now);
void views_again(struct viewset *s);                      /* the next view (not while the view takes typing) */
void views_goto(struct viewset *s, int v);
void views_pointer(struct viewset *s, uint64_t now);      /* a click on the bar picks a view; the rest is the view's */
void views_draw(struct viewset *s, uint64_t now);         /* the bar, then the view */

/* the page with views (LINEAGE), for the F keys and the title bar: the view set of a page, or 0 */
const struct viewset *views_of(int page);
const char *views_name(int page, int v);                  /* "" when there is no such view */
int  views_count(int page);
