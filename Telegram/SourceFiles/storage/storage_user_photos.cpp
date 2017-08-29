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
#include "storage/storage_user_photos.h"

#include "base/task_queue.h"

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
	auto position = base::find(_photoIds, photoId);
	if (position == _photoIds.end()) {
		_count = base::none;
	} else {
		if (_count) {
			--*_count;
		}
		_photoIds.erase(position);
	}
	sendUpdate();
}

void UserPhotos::List::removeAfter(PhotoId photoId) {
	auto position = base::find(_photoIds, photoId);
	if (position == _photoIds.end()) {
		_count = base::none;
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
	sliceUpdated.notify(update, true);
}

void UserPhotos::List::query(
		const UserPhotosQuery &query,
		base::lambda_once<void(UserPhotosResult&&)> &&callback) {
	auto result = UserPhotosResult {};
	result.count = _count;

	auto position = base::find(_photoIds, query.key.photoId);
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
	}
	base::TaskQueue::Main().Put(
		[
			callback = std::move(callback),
			result = std::move(result)
		]() mutable {
		callback(std::move(result));
	});
}

std::map<UserId, UserPhotos::List>::iterator
UserPhotos::enforceLists(UserId user) {
	auto result = _lists.find(user);
	if (result != _lists.end()) {
		return result;
	}
	result = _lists.emplace(user, List {}).first;
	subscribe(result->second.sliceUpdated, [this, user](const SliceUpdate &update) {
		sliceUpdated.notify(UserPhotosSliceUpdate(
			user,
			update.photoIds,
			update.count), true);
	});
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

void UserPhotos::query(
		const UserPhotosQuery &query,
		base::lambda_once<void(UserPhotosResult&&)> &&callback) {
	auto userIt = _lists.find(query.key.userId);
	if (userIt != _lists.end()) {
		userIt->second.query(query, std::move(callback));
	} else {
		base::TaskQueue::Main().Put(
			[
				callback = std::move(callback)
			]() mutable {
			callback(UserPhotosResult());
		});
	}
}

} // namespace Storage
