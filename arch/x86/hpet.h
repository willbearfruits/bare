#pragma once
#include <stdint.h>
#include <stdbool.h>
bool hpet_start_legacy(uint32_t hz);     /* HPET timer 0 on IRQ 0 at hz, in place of the 8254; false if there's none */
