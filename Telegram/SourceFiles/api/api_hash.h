/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Api {

uint32 HashInit() {
	return 0;
}

template <
	typename Int,
	typename = std::enable_if_t<
		std::is_same_v<Int, int32> || std::is_same_v<Int, uint32>>>
void HashUpdate(uint32 &already, Int value) {
	already += (already * 20261) + uint32(value);
}

int32 HashFinalize(uint32 already) {
	return int32(already & 0x7FFFFFFF);
}

template <typename IntRange>
inline int32 CountHash(IntRange &&range) {
	auto result = HashInit();
	for (const auto value : range) {
		HashUpdate(result, value);
	}
	return HashFinalize(result);
}

} // namespace Api
