/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_user_photos.h"

namespace Storage {

void UserPhotos::List::addNew(PhotoId photoId) {
	if (!base::contains(_photoIds, photoId)) {
		_photoIds.push_back(photoId);
		if (_count) {
			++*_count;
		}
		sendUpdate();
	}
}

void UserPhotos::List::addSlice(
		std::vector<PhotoId> &&photoIds,
		int count) {
	for (auto photoId : photoIds) {
		if (!base::contains(_photoIds, photoId)) {
			_photoIds.push_front(photoId);
		}
	}

	_count = count;
	if ((_count && *_count < _photoIds.size()) || photoIds.empty()) {
		_count = _photoIds.size();
	}
	sendUpdate();
}

void UserPhotos::List::removeOne(PhotoId photoId) {
	auto position = ranges::find(_photoIds, photoId);
	if (position == _photoIds.end()) {
		_count = std::nullopt;
	} else {
		if (_count) {
			--*_count;
		}
		_photoIds.erase(position);
	}
	sendUpdate();
}

void UserPhotos::List::removeAfter(PhotoId photoId) {
	auto position = ranges::find(_photoIds, photoId);
	if (position == _photoIds.end()) {
		_count = std::nullopt;
		_photoIds.clear();
	} else {
		if (_count) {
			*_count -= (_photoIds.end() - position);
		}
		_photoIds.erase(position, _photoIds.end());
	}
	sendUpdate();
}

void UserPhotos::List::sendUpdate() {
	auto update = SliceUpdate();
	update.photoIds = &_photoIds;
	update.count = _count;
	_sliceUpdated.fire(std::move(update));
}

rpl::producer<UserPhotosResult> UserPhotos::List::query(
		UserPhotosQuery &&query) const {
	return [this, query = std::move(query)](auto consumer) {
		auto result = UserPhotosResult {};
		result.count = _count;

		auto position = ranges::find(_photoIds, query.key.photoId);
		if (position != _photoIds.end()) {
			auto haveBefore = int(position - _photoIds.begin());
			auto haveEqualOrAfter = int(_photoIds.end() - position);
			auto before = qMin(haveBefore, query.limitBefore);
			auto equalOrAfter = qMin(haveEqualOrAfter, query.limitAfter + 1);
			result.photoIds = std::deque<PhotoId>(
				position - before,
				position + equalOrAfter);

			auto skippedInIds = (haveBefore - before);
			result.skippedBefore = _count
				| func::add(-int(_photoIds.size()) + skippedInIds);
			result.skippedBefore = haveBefore - before;
			result.skippedAfter = (haveEqualOrAfter - equalOrAfter);
			consumer.put_next(std::move(result));
		} else if (_count) {
			consumer.put_next(std::move(result));
		}
		consumer.put_done();
		return rpl::lifetime();
	};
}

auto UserPhotos::List::sliceUpdated() const -> rpl::producer<SliceUpdate> {
	return _sliceUpdated.events();
}

rpl::producer<UserPhotosSliceUpdate> UserPhotos::sliceUpdated() const {
	return _sliceUpdated.events();
}

std::map<UserId, UserPhotos::List>::iterator UserPhotos::enforceLists(UserId user) {
	auto result = _lists.find(user);
	if (result != _lists.end()) {
		return result;
	}
	result = _lists.emplace(user, List {}).first;
	result->second.sliceUpdated(
	) | rpl::start_with_next([this, user](
			const SliceUpdate &update) {
		_sliceUpdated.fire(UserPhotosSliceUpdate(
			user,
			update.photoIds,
			update.count));
	}, _lifetime);
	return result;
}

void UserPhotos::add(UserPhotosAddNew &&query) {
	auto userIt = enforceLists(query.userId);
	userIt->second.addNew(query.photoId);
}

void UserPhotos::add(UserPhotosAddSlice &&query) {
	auto userIt = enforceLists(query.userId);
	userIt->second.addSlice(
		std::move(query.photoIds),
		query.count);
}

void UserPhotos::remove(UserPhotosRemoveOne &&query) {
	auto userIt = _lists.find(query.userId);
	if (userIt != _lists.end()) {
		userIt->second.removeOne(query.photoId);
	}
}

void UserPhotos::remove(UserPhotosRemoveAfter &&query) {
	auto userIt = _lists.find(query.userId);
	if (userIt != _lists.end()) {
		userIt->second.removeAfter(query.photoId);
	}
}

rpl::producer<UserPhotosResult> UserPhotos::query(UserPhotosQuery &&query) const {
	auto userIt = _lists.find(query.key.userId);
	if (userIt != _lists.end()) {
		return userIt->second.query(std::move(query));
	}
	return [](auto consumer) {
		consumer.put_done();
		return rpl::lifetime();
	};
}

} // namespace Storage
