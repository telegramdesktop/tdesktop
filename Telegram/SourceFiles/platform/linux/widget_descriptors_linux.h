/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef __linux__

#include <sys/syscall.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unistd.h>

namespace Platform::WidgetHelper {

inline constexpr auto kDirectoryBufferSize = 4096;

struct DirectoryEntry {
	uint64_t inode = 0;
	int64_t offset = 0;
	uint16_t length = 0;
	uint8_t type = 0;
	char name[1] = {};
};

template <typename Visit>
[[nodiscard]] int ForEachEntry(int directory, Visit visit) {
	char buffer[kDirectoryBufferSize] = {};
	while (true) {
		const auto size = syscall(
			SYS_getdents64,
			directory,
			buffer,
			sizeof(buffer));
		if (size <= 0) {
			return (size < 0) ? errno : 0;
		}
		auto changed = false;
		for (auto offset = long(0); offset < size;) {
			auto length = uint16_t(0);
			std::memcpy(
				&length,
				buffer + offset + offsetof(DirectoryEntry, length),
				sizeof(length));
			if (!length) {
				return EINVAL;
			}
			if (visit(buffer + offset + offsetof(DirectoryEntry, name))) {
				changed = true;
			}
			offset += length;
		}
		if (changed && lseek(directory, 0, SEEK_SET) < 0) {
			return errno;
		}
	}
}

void CloseDescriptorsFrom(int first);

} // namespace Platform::WidgetHelper

#endif // __linux__
