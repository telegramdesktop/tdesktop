/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <time.h>

int __clock_gettime_glibc_old(clockid_t clk_id, struct timespec *tp);
__asm__(".symver __clock_gettime_glibc_old,clock_gettime@GLIBC_2.2.5");

int __wrap_clock_gettime(clockid_t clk_id, struct timespec *tp) {
        return __clock_gettime_glibc_old(clk_id, tp);
}

