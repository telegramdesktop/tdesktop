/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include <time.h>
#include <stdint.h>

int __clock_gettime_glibc_old(clockid_t clk_id, struct timespec *tp);
__asm__(".symver __clock_gettime_glibc_old,clock_gettime@GLIBC_2.2");

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

