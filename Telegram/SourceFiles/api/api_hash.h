/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Api {

[[nodiscard]] uint64 CountStickersHash(
	not_null<Main::Session*> session,
	bool checkOutdatedInfo = false);
[[nodiscard]] uint64 CountMasksHash(
	not_null<Main::Session*> session,
	bool checkOutdatedInfo = false);
[[nodiscard]] uint64 CountRecentStickersHash(
	not_null<Main::Session*> session,
	bool attached = false);
[[nodiscard]] uint64 CountFavedStickersHash(not_null<Main::Session*> session);
[[nodiscard]] uint64 CountFeaturedStickersHash(
	not_null<Main::Session*> session);
[[nodiscard]] uint64 CountSavedGifsHash(not_null<Main::Session*> session);

[[nodiscard]] inline uint64 HashInit() {
	return 0;
}

inline void HashUpdate(uint64 &already, uint64 value) {
	already ^= (already >> 21);
	already ^= (already << 35);
	already ^= (already >> 4);
	already += value;
}

inline void HashUpdate(uint64 &already, int64 value) {
	HashUpdate(already, uint64(value));
}

inline void HashUpdate(uint64 &already, uint32 value) {
	HashUpdate(already, uint64(value));
}

inline void HashUpdate(uint64 &already, int32 value) {
	HashUpdate(already, int64(value));
}

[[nodiscard]] inline uint64 HashFinalize(uint64 already) {
	return already;
}

template <typename IntRange>
[[nodiscard]] inline uint64 CountHash(IntRange &&range) {
	auto result = HashInit();
	for (const auto value : range) {
		HashUpdate(result, value);
	}
	return HashFinalize(result);
}

} // namespace Api
