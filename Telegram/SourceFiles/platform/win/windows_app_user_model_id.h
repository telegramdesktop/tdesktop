/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <wtypes.h>

namespace Platform {
namespace AppUserModelId {

void CleanupShortcut();
void CheckPinned();

[[nodiscard]] const std::wstring &Id();
bool ValidateShortcut();

[[nodiscard]] const PROPERTYKEY &Key();

[[nodiscard]] const std::wstring &MyExecutablePath();

struct UniqueFileId {
	std::uint64_t part1 = 0;
	std::uint64_t part2 = 0;

	[[nodiscard]] bool valid() const {
		return part1 || part2;
	}
	[[nodiscard]] explicit operator bool() const {
		return valid();
	}

	[[nodiscard]] friend inline auto operator<=>(
		UniqueFileId a,
		UniqueFileId b) = default;
	[[nodiscard]] friend inline bool operator==(
		UniqueFileId a,
		UniqueFileId b) = default;
};

[[nodiscard]] UniqueFileId GetUniqueFileId(LPCWSTR path);
[[nodiscard]] UniqueFileId MyExecutablePathId();

} // namespace AppUserModelId
} // namespace Platform
