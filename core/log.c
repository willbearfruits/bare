#include "log.h"

static char ring[16384];                    /* power of two */
static uint32_t head;                       /* characters ever written */

void log_keep(char c) { ring[head & (sizeof ring - 1)] = c; head++; }

int log_line(int back, char *out, int cap) {
    const uint32_t mask = sizeof ring - 1, oldest = head > sizeof ring ? head - (uint32_t)sizeof ring : 0;
    if (head == oldest || cap < 1) return -1;
    uint32_t end = head;                    /* one past the line's last character */
    if (ring[(end - 1) & mask] == '\n') end--;
    for (;;) {
        uint32_t start = end;
        while (start > oldest && ring[(start - 1) & mask] != '\n') start--;
        if (back-- == 0) {
            int n = 0;
            for (uint32_t k = start; k < end && n < cap - 1; k++) out[n++] = ring[k & mask];
            out[n] = 0;
            return n;
        }
        if (start <= oldest) return -1;
        end = start - 1;                    /* past the newline that ends the line before */
    }
}
