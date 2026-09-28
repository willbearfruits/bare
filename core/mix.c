#include "mix.h"
#include "libc.h"
#include "platform.h"
#include "log.h"

struct mix_state mix;
const char *const mix_names[MIX_CHANNELS] = { "PLAY", "SEQ", "RHYTHM", "INPUT", "STRETCH", "TAPE", "TOUCH", "ANS", "UPIC", "CLOUDS", "DOOM", "LINEAGE" };

/* 4096 × 10^(dB/20), -60 .. +12 dB */
static const int16_t gain_q12[MIX_DB_MAX - MIX_DB_MIN + 1] = {
    4, 5, 5, 6, 6, 7, 8, 9, 10, 12, 13, 15, 16, 18, 21, 23, 26, 29, 33, 37, 41, 46, 52, 58, 65, 73, 82, 92, 103, 115,
    130, 145, 163, 183, 205, 230, 258, 290, 325, 365, 410, 460, 516, 579, 649, 728, 817, 917, 1029, 1154, 1295, 1453,
    1631, 1830, 2053, 2303, 2584, 2900, 3254, 3651, 4096, 4596, 5157, 5786, 6492, 7284, 8173, 9170, 10289, 11544,
    12953, 14533, 16306 };
int32_t mix_gain_q12(int db) { return db < MIX_DB_MIN ? 0 : gain_q12[MIN(db, MIX_DB_MAX) - MIX_DB_MIN]; }

void mix_init(void) {
    memset(&mix, 0, sizeof mix);
    for (int c = 0; c < MIX_CHANNELS; c++) { mix.ch[c].db = 0; mix.ch[c].echo = 100; mix.ch[c].gl = mix.ch[c].gr = 4096; }
    mix.ch[CH_INPUT].mute = true;                   /* heard only when asked for: a laptop's mic would howl */
    mix.input = -1;
}

bool mix_heard(int c) {
    bool solo = false;
    for (int i = 0; i < MIX_CHANNELS; i++) solo |= mix.ch[i].solo;
    return solo ? mix.ch[c].solo : !mix.ch[c].mute;
}

bool mix_set_input(int i) {
    if (!plat_audio_input(i)) { logf("mix: input %d not available", i); return false; }
    mix.input = (int8_t)(i < 0 ? -1 : i);
    return true;
}
