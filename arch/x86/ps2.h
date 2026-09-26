#pragma once
#include <stdbool.h>
#include "platform.h"
void ps2_init(void);
bool ps2_key_poll(struct key_event *ev);
bool ps2_pointer_poll(struct pointer_event *ev);
/* other keyboards and pointers (USB) join the same queues, from the main loop */
void ps2_inject_key(uint8_t code, bool down);
void ps2_inject_pointer(const struct pointer_event *e);
bool ps2_has_touchpad(void);                    /* a Synaptics pad in absolute mode */
