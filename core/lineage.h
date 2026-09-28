#pragma once
/* LINEAGE (F11 by default): homages, each a view that plays like its instrument and says where it comes from —
   XENAKIS (five views of its own: core/xen.h; F12 opens it by default), ANS (Evgeny Murzin's photoelectronic
   synthesizer, and Coil), REICH (phasing), CARLOS (the Moog, and her scales without octaves), RADIGUE (slow beating
   drones) and MERZBOW (noise from junk). In the order of the music they come from. */
#include "views.h"

extern const struct view lin_xen, lin_ans, lin_reich, lin_carlos, lin_radigue, lin_merzbow;
enum { LV_XEN, LV_ANS, LV_REICH, LV_CARLOS, LV_RADIGUE, LV_MERZBOW, LV_COUNT };
const struct viewset *lineage_views(void);
int  lineage_current(void);
void lineage_goto(int view);

/* where a homage view puts its parts, in cells: the picture on the left, its knobs on the right, the history below */
struct lin_layout { int x, y, pw, ph, kx, kw, ly, lh; };
void lineage_layout(struct lin_layout *L, int knob_w);
/* the homages' sound: the audio loop's per-block work (before the voices), and what their engines render into the
   LINEAGE bus (core/lineage_audio.c) */
void lineage_block(uint32_t n);
void lineage_run_events(void);             /* REICH's players: what is due at this frame */
uint32_t lineage_next_event(void);         /* frames to the next */
void lineage_advance(uint32_t n);
bool lineage_render(int32_t *l, int32_t *r, uint32_t n, bool add);
/* a knob panel's rows: the chosen one lit, "name  value" */
void lineage_knob(int x, int y, int w, const char *name, const char *value, bool on);
