#pragma once
/* VMware/QEMU absolute mouse (the "vmmouse" backdoor). Present in QEMU with -device vmmouse; harmless elsewhere. */
#include <stdbool.h>
#include "platform.h"
bool vmmouse_init(void);
bool vmmouse_active(void);
bool vmmouse_poll(struct pointer_event *ev);   /* one absolute event, if any queued */
