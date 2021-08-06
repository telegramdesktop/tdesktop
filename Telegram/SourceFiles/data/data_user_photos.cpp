/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_user_photos.h"

#include "main/main_session.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "data/data_user.h"
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
		std::optional<int> count,
		const std::deque<PhotoId> &photoIds,
		std::optional<int> skippedBefore,
		int skippedAfter);
	void sliceToLimits();

	Key _key;
	std::deque<PhotoId> _ids;
	std::optional<int> _fullCount;
	std::optional<int> _skippedBefore;
	int _skippedAfter = 0;
	int _limitBefore = 0;
	int _limitAfter = 0;

	rpl::event_stream<PhotoId> _insufficientPhotosAround;

};

UserPhotosSlice::UserPhotosSlice(Key key)
: UserPhotosSlice(
	key,
	{},
	std::nullopt,
	std::nullopt,
	std::nullopt) {
}

UserPhotosSlice::UserPhotosSlice(
	Key key,
	std::deque<PhotoId> &&ids,
	std::optional<int> fullCount,
	std::optional<int> skippedBefore,
	std::optional<int> skippedAfter)
: AbstractSparseIds<std::deque<PhotoId>>(
	ids,
	fullCount,
	skippedBefore,
	skippedAfter)
, _key(key) {
}

std::optional<int> UserPhotosSlice::distance(
		const Key &a,
		const Key &b) const {
	if (a.userId != _key.userId
		|| b.userId != _key.userId) {
		return std::nullopt;
	}
	if (const auto i = indexOf(a.photoId)) {
		if (const auto j = indexOf(b.photoId)) {
			return *j - *i;
		}
	}
	return std::nullopt;
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
		std::optional<int> count,
		const std::deque<PhotoId> &photoIds,
		std::optional<int> skippedBefore,
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
		not_null<Main::Session*> session,
		UserPhotosSlice::Key key,
		int limitBefore,
		int limitAfter) {
	return [=](auto consumer) {
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
		auto requestPhotosAround = [user = session->data().user(key.userId)](
				PhotoId photoId) {
			user->session().api().requestUserPhotos(user, photoId);
		};
		builder->insufficientPhotosAround()
			| rpl::start_with_next(requestPhotosAround, lifetime);

		session->storage().userPhotosSliceUpdated()
			| rpl::start_with_next(applyUpdate, lifetime);

		session->storage().query(Storage::UserPhotosQuery(
			key,
			limitBefore,
			limitAfter
		)) | rpl::start_with_next_done(
			applyUpdate,
			[=] { builder->checkInsufficientPhotos(); },
			lifetime);

		return lifetime;
	};
}


rpl::producer<UserPhotosSlice> UserPhotosReversedViewer(
		not_null<Main::Session*> session,
		UserPhotosSlice::Key key,
		int limitBefore,
		int limitAfter) {
	return UserPhotosViewer(
		session,
		key,
		limitBefore,
		limitAfter
	) | rpl::map([](UserPhotosSlice &&slice) {
		slice.reverse();
		return std::move(slice);
	});
}
