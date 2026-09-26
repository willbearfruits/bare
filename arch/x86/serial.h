#pragma once
#include <stdbool.h>
void serial_init(void);
void serial_putc(char c);
void serial_log(bool on);          /* the log off COM1 while MIDI uses it */
