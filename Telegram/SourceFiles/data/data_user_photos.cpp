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
#include "data/data_user_photos.h"

#include "auth_session.h"
#include "apiwrap.h"
#include "storage/storage_facade.h"
#include "storage/storage_user_photos.h"

class UserPhotosSliceBuilder {
public:
	using Key = UserPhotosSlice::Key;

	UserPhotosSliceBuilder(Key key, int limitBefore, int limitAfter);

	bool applyUpdate(const Storage::UserPhotosResult &update);
	bool applyUpdate(const Storage::UserPhotosSliceUpdate &update);
	void checkInsufficientPhotos();
	auto insufficientPhotosAround() const {
		return _insufficientPhotosAround.events();
	}

	UserPhotosSlice snapshot() const;

private:
	void mergeSliceData(
		base::optional<int> count,
		const std::deque<PhotoId> &photoIds,
		base::optional<int> skippedBefore,
		int skippedAfter);
	void sliceToLimits();

	Key _key;
	std::deque<PhotoId> _ids;
	base::optional<int> _fullCount;
	base::optional<int> _skippedBefore;
	int _skippedAfter = 0;
	int _limitBefore = 0;
	int _limitAfter = 0;

	rpl::event_stream<PhotoId> _insufficientPhotosAround;

};

UserPhotosSlice::UserPhotosSlice(Key key)
: UserPhotosSlice(
	key,
	{},
	base::none,
	base::none,
	base::none) {
}

UserPhotosSlice::UserPhotosSlice(
	Key key,
	std::deque<PhotoId> &&ids,
	base::optional<int> fullCount,
	base::optional<int> skippedBefore,
	base::optional<int> skippedAfter)
: _key(key)
, _ids(std::move(ids))
, _fullCount(fullCount)
, _skippedBefore(skippedBefore)
, _skippedAfter(skippedAfter) {
}

void UserPhotosSlice::reverse() {
	ranges::reverse(_ids);
	std::swap(_skippedBefore, _skippedAfter);
}

base::optional<int> UserPhotosSlice::indexOf(PhotoId photoId) const {
	auto it = ranges::find(_ids, photoId);
	if (it != _ids.end()) {
		return (it - _ids.begin());
	}
	return base::none;
}

PhotoId UserPhotosSlice::operator[](int index) const {
	Expects(index >= 0 && index < size());

	return *(_ids.begin() + index);
}

base::optional<int> UserPhotosSlice::distance(const Key &a, const Key &b) const {
	if (a.userId != _key.userId
		|| b.userId != _key.userId) {
		return base::none;
	}
	if (auto i = indexOf(a.photoId)) {
		if (auto j = indexOf(b.photoId)) {
			return *j - *i;
		}
	}
	return base::none;
}

UserPhotosSliceBuilder::UserPhotosSliceBuilder(
	Key key,
	int limitBefore,
	int limitAfter)
: _key(key)
, _limitBefore(limitBefore)
, _limitAfter(limitAfter) {
}

bool UserPhotosSliceBuilder::applyUpdate(const Storage::UserPhotosResult &update) {
	mergeSliceData(
		update.count,
		update.photoIds,
		update.skippedBefore,
		update.skippedAfter);
	return true;
}

bool UserPhotosSliceBuilder::applyUpdate(const Storage::UserPhotosSliceUpdate &update) {
	if (update.userId != _key.userId) {
		return false;
	}
	auto idsCount = update.photoIds ? int(update.photoIds->size()) : 0;
	mergeSliceData(
		update.count,
		update.photoIds ? *update.photoIds : std::deque<PhotoId> {},
		update.count | func::add(-idsCount),
		0);
	return true;
}

void UserPhotosSliceBuilder::checkInsufficientPhotos() {
	sliceToLimits();
}

void UserPhotosSliceBuilder::mergeSliceData(
		base::optional<int> count,
		const std::deque<PhotoId> &photoIds,
		base::optional<int> skippedBefore,
		int skippedAfter) {
	if (photoIds.empty()) {
		if (_fullCount != count) {
			_fullCount = count;
			if (_fullCount && *_fullCount <= _ids.size()) {
				_fullCount = _ids.size();
				_skippedBefore = _skippedAfter = 0;
			}
		}
	} else {
		if (count) {
			_fullCount = count;
		}
		_skippedAfter = skippedAfter;
		_ids = photoIds;

		if (_fullCount) {
			_skippedBefore = *_fullCount
				- _skippedAfter
				- int(_ids.size());
		}
	}
	sliceToLimits();
}

void UserPhotosSliceBuilder::sliceToLimits() {
	auto aroundIt = ranges::find(_ids, _key.photoId);
	auto removeFromBegin = (aroundIt - _ids.begin() - _limitBefore);
	auto removeFromEnd = (_ids.end() - aroundIt - _limitAfter - 1);
	if (removeFromEnd > 0) {
		_ids.erase(_ids.end() - removeFromEnd, _ids.end());
		_skippedAfter += removeFromEnd;
	}
	if (removeFromBegin > 0) {
		_ids.erase(_ids.begin(), _ids.begin() + removeFromBegin);
		if (_skippedBefore) {
			*_skippedBefore += removeFromBegin;
		}
	} else if (removeFromBegin < 0 && (!_skippedBefore || *_skippedBefore > 0)) {
		_insufficientPhotosAround.fire(_ids.empty() ? 0 : _ids.front());
	}
}

UserPhotosSlice UserPhotosSliceBuilder::snapshot() const {
	return UserPhotosSlice(
		_key,
		base::duplicate(_ids),
		_fullCount,
		_skippedBefore,
		_skippedAfter);
}

rpl::producer<UserPhotosSlice> UserPhotosViewer(
		UserPhotosSlice::Key key,
		int limitBefore,
		int limitAfter) {
	return [key, limitBefore, limitAfter](auto consumer) {
		auto lifetime = rpl::lifetime();
		auto builder = lifetime.make_state<UserPhotosSliceBuilder>(
			key,
			limitBefore,
			limitAfter);
		auto applyUpdate = [=](auto &&update) {
			if (builder->applyUpdate(std::forward<decltype(update)>(update))) {
				consumer.put_next(builder->snapshot());
			}
		};
		auto requestPhotosAround = [user = App::user(key.userId)](
				PhotoId photoId) {
			Auth().api().requestUserPhotos(user, photoId);
		};
		builder->insufficientPhotosAround()
			| rpl::start_with_next(requestPhotosAround, lifetime);

		Auth().storage().userPhotosSliceUpdated()
			| rpl::start_with_next(applyUpdate, lifetime);

		Auth().storage().query(Storage::UserPhotosQuery(
			key,
			limitBefore,
			limitAfter))
			| rpl::start_with_next_done(
				applyUpdate,
				[=] { builder->checkInsufficientPhotos(); },
				lifetime);

		return lifetime;
	};
}


rpl::producer<UserPhotosSlice> UserPhotosReversedViewer(
		UserPhotosSlice::Key key,
		int limitBefore,
		int limitAfter) {
	return UserPhotosViewer(key, limitBefore, limitAfter)
		| rpl::map([](UserPhotosSlice &&slice) {
			slice.reverse();
			return std::move(slice);
		});
}