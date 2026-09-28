/* The LINEAGE views' sound: each engine's per-block work, and what the engines render into the LINEAGE bus. */
#include "lineage.h"
#include "carlos.h"
#include "junk.h"
#include "drone.h"
#include "phase.h"

void lineage_block(uint32_t n) { carlos_block(n); }
/* REICH's players are event sources on the sample clock, as the sequencer and the rhythm section are */
void lineage_run_events(void) { phase_run_events(); }
uint32_t lineage_next_event(void) { return phase_next_event(); }
void lineage_advance(uint32_t n) { phase_advance(n); }
/* MERZBOW's chain takes what is on the bus (its own sources, CARLOS's voices while it plays); RADIGUE's drone adds in
   after it, never crushed */
bool lineage_render(int32_t *l, int32_t *r, uint32_t n, bool add) { return drone_render(l, r, n, junk_render(l, r, n, add)); }
