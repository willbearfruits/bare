#pragma once
#include "platform.h"
bool rtc_read(struct rtc_time *t);   /* CMOS real-time clock (ports 0x70/0x71) */
