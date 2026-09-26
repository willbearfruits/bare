#include "sound.h"
#include "hda.h"

const struct sound *sound;

const struct sound *sound_init(void) {
    if (hda_init()) sound = &sound_hda;
    else if (ac97_init()) sound = &sound_ac97;
    else if (sb16_init()) sound = &sound_sb16;
    return sound;
}
