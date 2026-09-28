/* Keys that follow the chord (see harmony.h). The chord is read from the omnichord's published word, so the audio side
   (the clouds) can snap too. */
#include "harmony.h"
#include "omni.h"
#include "libc.h"

bool harmony_on;
bool harmony_active(void) { return harmony_on; }

uint16_t harmony_pcs(void) {
    uint8_t pcs[3]; omni_chord_tones(omni_chord_word, pcs);
    return (uint16_t)(1u << pcs[0] | 1u << pcs[1] | 1u << pcs[2]);
}

/* the nearest pitch class in the chord: up to 6 semitones away either side, ties up */
static int nearest(int n, uint16_t set) {
    for (int d = 0; d <= 6; d++) {
        if (set >> ((n + d) % 12 + 12) % 12 & 1) return n + d;
        if (set >> ((n - d) % 12 + 12) % 12 & 1) return n - d;
    }
    return n;
}
int harmony_note(int note) {
    if (!harmony_on || note < 0) return note;
    int n = nearest(note, harmony_pcs());
    return n < 0 ? n + 12 : n > 127 ? n - 12 : n;
}
int32_t harmony_snap_q8(int32_t q8) {
    if (!harmony_on) return q8;
    int n = (q8 + 128) >> 8;                                  /* the nearest semitone, then its chord tone */
    return (int32_t)nearest(n, harmony_pcs()) * 256;
}

const char *harmony_label(void) {
    static char l[16];
    char name[12]; omni_chord_name(name);
    snfmt(l, sizeof l, "♪ %s", name);
    return l;
}
