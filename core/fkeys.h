#pragma once
/* What each F key opens. F1 … F12 (Ctrl+1 … 9, 0, -, = are the same twelve) each open a page, one of the instruments
   (PLAY showing it), a view of LINEAGE (XENAKIS, or one of XENAKIS's own views) — or nothing. By default F1 PLAY …
   F11 LINEAGE in ui_pages' order, and F12 LINEAGE's XENAKIS (a page of its own until it moved inside). The layout
   is KEYS.TXT in the root of the stick's FAT partition, a line for each key that differs from the default ("F9
   SHRUTI", "F10 off"): read after the instruments at boot and on every rescan, and written a moment after a change
   (the FILE page's KEYS view, a tab dragged in the title bar). FILE is always on a key. An instrument is kept by its
   name and looked for when its key is pressed. */
#include <stdint.h>
#include <stdbool.h>

#define FKEYS 12
enum { FK_OFF, FK_PAGE, FK_INST, FK_VIEW };
struct fkey {
    uint8_t kind, page, view;                /* FK_PAGE: page (PAGE_*); FK_VIEW: that page's view (LV_*) */
    uint8_t sub;                             /* FK_VIEW of XENAKIS: 0 as it was left, or one of its views + 1 (XV_*) */
    char    inst[12];                        /* FK_INST: the instrument's name */
    int8_t  left_at;                         /* FK_PAGE, while running: where in its page it was left (PLAY's instrument
                                                + 1, LINEAGE's view), -1 as the page is */
};
extern struct fkey fkeys[FKEYS];
extern const char *const fkey_names[FKEYS];  /* "F1" … "F12" */
extern char fkeys_status[96];                /* where the layout came from, or went: for the KEYS view */
extern bool fkeys_trouble;                   /* that says something went wrong: mistakes in the file, a failed write */

void fkeys_default(struct fkey out[FKEYS]);
int  fkey_page(const struct fkey *k);        /* the page it opens, -1 when off */
bool fkey_same(const struct fkey *a, const struct fkey *b);   /* the same place (left_at aside) */
const char *fkey_label(const struct fkey *k);                 /* "PLAY", "SHRUTI", "UPIC"; "" when off */
const char *fkey_short(const struct fkey *k);                 /* the title bar's where the labels don't fit */
const char *fkey_about(const struct fkey *k);                 /* a few words on what it is */
int  fkey_inst(const struct fkey *k);         /* FK_INST: its instrument's index now, -1 if none has that name */
int  fkeys_for_page(int page);               /* a key opening that page: one opening it whole first; -1: none */
const char *fkeys_page_key(int page);        /* that key's name ("F4"), or 0 when no key opens it */
void fkeys_swap(int a, int b);
bool fkeys_file_elsewhere(int key);          /* FILE is on another key than this one */

/* the places a key can open, in the KEYS view's order: off, the pages, LINEAGE's views, XENAKIS's, the instruments */
int  fkeys_places(void);
void fkeys_place(int i, struct fkey *out);
int  fkeys_place_of(const struct fkey *k);   /* its index in that order; 0 (off) for an instrument that is gone */

/* KEYS.TXT */
int  fkeys_parse(const char *text, int len, struct fkey out[FKEYS], char *err, int cap);   /* mistakes; the first in err */
int  fkeys_text(char *out, int cap);         /* the layout as the file: only the keys off their default */
void fkeys_load(void);                       /* from the stick, when it has the file; otherwise the layout stays */
void fkeys_changed(uint64_t now);            /* the layout changed: written to the stick a moment later */
void fkeys_work(uint64_t now);               /* main loop: that write */
bool fkeys_pending(void);                    /* a change not written yet */
