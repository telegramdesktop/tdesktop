/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Api {

[[nodiscard]] inline uint32 HashInit() {
	return 0;
}

inline void HashUpdate(uint32 &already, uint32 value) {
	already = (already * 20261) + uint32(value);
}

inline void HashUpdate(uint32 &already, int32 value) {
	HashUpdate(already, uint32(value));
}

inline void HashUpdate(uint32 &already, uint64 value) {
	HashUpdate(already, uint32(value >> 32));
	HashUpdate(already, uint32(value & 0xFFFFFFFFULL));
}

[[nodiscard]] inline int32 HashFinalize(uint32 already) {
	return int32(already & 0x7FFFFFFF);
}

template <typename IntRange>
[[nodiscard]] inline int32 CountHash(IntRange &&range) {
	auto result = HashInit();
	for (const auto value : range) {
		HashUpdate(result, value);
	}
	return HashFinalize(result);
}

} // namespace Api
