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
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

void *__wrap_aligned_alloc(size_t alignment, size_t size) {
	void *result = NULL;
	return (posix_memalign(&result, alignment, size) == 0)
		? result
		: NULL;
}

int enable_secure_inited = 0;
int enable_secure = 1;

char *__wrap_secure_getenv(const char *name) {
	if (enable_secure_inited == 0) {
		enable_secure_inited = 1;
		enable_secure = (geteuid() != getuid())
		|| (getegid() != getgid());
	}
	return enable_secure ? NULL : getenv(name);
}

