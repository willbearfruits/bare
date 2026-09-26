/* CMOS real-time clock: gives project saves a real date. Handles BCD/binary and 12/24-hour modes. */
#include "rtc.h"
#include "io.h"

static uint8_t cmos(uint8_t reg) { outb(0x70, reg | 0x80); return inb(0x71); }   /* bit 7: keep NMI masked */

static bool read_once(struct rtc_time *t) {
    for (int i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++) ;              /* update in progress */
    uint8_t s = cmos(0x00), m = cmos(0x02), h = cmos(0x04), d = cmos(0x07), mo = cmos(0x08), y = cmos(0x09), b = cmos(0x0B);
    bool pm = h & 0x80; h &= 0x7F;
    if (!(b & 0x04)) {                                                       /* BCD */
        #define BCD(x) ((uint8_t)(((x) >> 4) * 10 + ((x) & 15)))
        s = BCD(s); m = BCD(m); h = BCD(h); d = BCD(d); mo = BCD(mo); y = BCD(y);
    }
    if (!(b & 0x02)) { if (pm && h < 12) h += 12; if (!pm && h == 12) h = 0; }   /* 12-hour clock */
    if (s > 59 || m > 59 || h > 23 || d < 1 || d > 31 || mo < 1 || mo > 12) return false;
    t->sec = s; t->min = m; t->hour = h; t->day = d; t->month = mo; t->year = (uint16_t)(2000 + y);
    return true;
}

bool rtc_read(struct rtc_time *t) {
    struct rtc_time a, b;
    if (!read_once(&a)) return false;
    if (!read_once(&b)) return false;
    if (a.sec != b.sec || a.min != b.min) return read_once(t);            /* rolled over between reads: once more */
    *t = b;
    return true;
}
