/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/widget_descriptors_linux.h"

#ifdef __linux__

#include <sys/resource.h>
#include <sys/syscall.h>

#include <algorithm>
#include <climits>
#include <fcntl.h>
#include <unistd.h>

#ifndef __NR_close_range
#define __NR_close_range 436
#endif // __NR_close_range

namespace Platform::WidgetHelper {
namespace {

constexpr auto kMaxClosedDescriptor = rlim_t(1) << 20;

[[nodiscard]] int ParseDescriptor(const char *name) {
	if (!*name) {
		return -1;
	}
	auto result = 0;
	for (; *name; ++name) {
		const auto digit = *name - '0';
		if (digit < 0 || digit > 9 || result > (INT_MAX - digit) / 10) {
			return -1;
		}
		result = result * 10 + digit;
	}
	return result;
}

[[nodiscard]] bool CloseListed(int directory, int first) {
	const auto error = ForEachEntry(directory, [&](const char *name) {
		const auto descriptor = ParseDescriptor(name);
		if (descriptor < first || descriptor == directory) {
			return false;
		}
		close(descriptor);
		return true;
	});
	return !error;
}

[[nodiscard]] bool CloseListedDescriptors(int first) {
	const auto directory = open(
		"/proc/self/fd",
		O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (directory < 0) {
		return false;
	}
	const auto result = CloseListed(directory, first);
	close(directory);
	return result;
}

void CloseUpToLimit(int first) {
	auto limit = rlimit();
	const auto last = getrlimit(RLIMIT_NOFILE, &limit)
		? kMaxClosedDescriptor
		: std::min(limit.rlim_cur, kMaxClosedDescriptor);
	for (auto descriptor = rlim_t(first); descriptor < last; ++descriptor) {
		close(int(descriptor));
	}
}

} // namespace

void CloseDescriptorsFrom(int first) {
	if (syscall(__NR_close_range, first, ~0U, 0) == 0
		|| CloseListedDescriptors(first)) {
		return;
	}
	CloseUpToLimit(first);
}

} // namespace Platform::WidgetHelper

#endif // __linux__
