/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <time.h>
#include <stdint.h>

#if defined(_M_IX86) || defined(__i386__)
#define GETTIME_GLIBC_VERSION "2.2"
#elif defined(_M_ARM) || defined(__arm__)
#define GETTIME_GLIBC_VERSION "2.4"
#else
#error Please add glibc wraps for your architecture
#endif

int __clock_gettime_glibc_old(clockid_t clk_id, struct timespec *tp);
__asm__(".symver __clock_gettime_glibc_old,clock_gettime@GLIBC_" GETTIME_GLIBC_VERSION);

int __wrap_clock_gettime(clockid_t clk_id, struct timespec *tp) {
	return __clock_gettime_glibc_old(clk_id, tp);
}

uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem_p);

int64_t __wrap___divmoddi4(int64_t num, int64_t den, int64_t *rem_p) {
	int minus = 0;
	int64_t v;

	if (num < 0) {
		num = -num;
		minus = 1;
	}
	if (den < 0) {
		den = -den;
		minus ^= 1;
	}

	v = __udivmoddi4(num, den, (uint64_t *)rem_p);
	if (minus) {
		v = -v;
		if (rem_p)
			*rem_p = -(*rem_p);
	}

	return v;
}

