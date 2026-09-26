#pragma once
/* Install: the stick the instrument runs from, copied onto another drive (the computer's own disk), so the computer
   starts into it without the stick. Everything on the target is lost: the page asks for ERASE to be typed first.
   Copied as they are: the MBR's boot code, Limine's stage 2 in the gap after it and the FAT boot partition (kernels,
   limine.conf, the EFI loaders), then the project partition's header, its slots and the frames they use. The partition
   table is then the stick's with the project partition reaching the end of the target (MBR's 2 TiB at most), under a
   disk signature of its own; a stale GPT at the end of the target is wiped (firmware might "repair" from it). The copy
   runs a piece per main-loop pass (install_work), so the screen and the sound go on. */
#include <stdint.h>
#include <stdbool.h>

struct install_state {
    bool running, done, failed;
    int target;
    uint64_t total, copied;                          /* sectors, for the progress bar */
    char status[96];
};
extern struct install_state inst;

void install_describe(int drive, char *out, int cap);   /* what is on a drive now: "empty", "GPT, 4 partitions" … */
bool install_start(int drive);                           /* false: inst.status says why */
void install_cancel(void);
void install_work(void);                                 /* the main loop: a piece of the copy */
