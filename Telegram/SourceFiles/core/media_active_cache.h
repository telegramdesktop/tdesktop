/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/last_used_cache.h"

namespace Core {

template <typename Type>
class MediaActiveCache {
public:
	template <typename Unload>
	MediaActiveCache(int64 limit, Unload &&unload);

	void up(Type *entry);
	void remove(Type *entry);
	void clear();

	void increment(int64 amount);
	void decrement(int64 amount);

private:
	template <typename Unload>
	void check(Unload &&unload);

	base::last_used_cache<Type*> _cache;
	SingleQueuedInvokation _delayed;
	int64 _usage = 0;
	int64 _limit = 0;

};

template <typename Type>
template <typename Unload>
MediaActiveCache<Type>::MediaActiveCache(int64 limit, Unload &&unload)
: _delayed([=] { check(unload); })
, _limit(limit) {
}

template <typename Type>
void MediaActiveCache<Type>::up(Type *entry) {
	_cache.up(entry);
	_delayed.call();
}

template <typename Type>
void MediaActiveCache<Type>::remove(Type *entry) {
	_cache.remove(entry);
}

template <typename Type>
void MediaActiveCache<Type>::clear() {
	_cache.clear();
}

template <typename Type>
void MediaActiveCache<Type>::increment(int64 amount) {
	_usage += amount;
}

template <typename Type>
void MediaActiveCache<Type>::decrement(int64 amount) {
	_usage -= amount;
}

template <typename Type>
template <typename Unload>
void MediaActiveCache<Type>::check(Unload &&unload) {
	while (_usage > _limit) {
		if (const auto entry = _cache.take_lowest()) {
			unload(entry);
		} else {
			break;
		}
	}
}

} // namespace Images
