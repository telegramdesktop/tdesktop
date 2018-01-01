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
#pragma once

#include <rpl/event_stream.h>
#include "storage/storage_facade.h"

namespace Storage {

struct UserPhotosAddNew {
	UserPhotosAddNew(UserId userId, PhotoId photoId)
		: userId(userId), photoId(photoId) {
	}

	UserId userId = 0;
	PhotoId photoId = 0;

};

struct UserPhotosAddSlice {
	UserPhotosAddSlice(
		UserId userId,
		std::vector<PhotoId> &&photoIds,
		int count)
		: userId(userId)
		, photoIds(std::move(photoIds))
		, count(count) {
	}

	UserId userId = 0;
	std::vector<PhotoId> photoIds;
	int count = 0;

};

struct UserPhotosRemoveOne {
	UserPhotosRemoveOne(
		UserId userId,
		PhotoId photoId)
		: userId(userId)
		, photoId(photoId) {
	}

	UserId userId = 0;
	PhotoId photoId = 0;

};

struct UserPhotosRemoveAfter {
	UserPhotosRemoveAfter(
		UserId userId,
		PhotoId photoId)
		: userId(userId)
		, photoId(photoId) {
	}

	UserId userId = 0;
	PhotoId photoId = 0;

};

struct UserPhotosKey {
	UserPhotosKey(
		UserId userId,
		PhotoId photoId)
		: userId(userId)
		, photoId(photoId) {
	}

	bool operator==(const UserPhotosKey &other) const {
		return (userId == other.userId)
			&& (photoId == other.photoId);
	}
	bool operator!=(const UserPhotosKey &other) const {
		return !(*this == other);
	}

	PeerId userId = 0;
	PhotoId photoId = 0;

};

struct UserPhotosQuery {
	UserPhotosQuery(
		UserPhotosKey key,
		int limitBefore,
		int limitAfter)
		: key(key)
		, limitBefore(limitBefore)
		, limitAfter(limitAfter) {
	}

	UserPhotosKey key;
	int limitBefore = 0;
	int limitAfter = 0;

};

struct UserPhotosResult {
	base::optional<int> count;
	base::optional<int> skippedBefore;
	int skippedAfter = 0;
	std::deque<PhotoId> photoIds;
};

struct UserPhotosSliceUpdate {
	UserPhotosSliceUpdate(
		UserId userId,
		const std::deque<PhotoId> *photoIds,
		base::optional<int> count)
		: userId(userId)
		, photoIds(photoIds)
		, count(count) {
	}

	UserId userId = 0;
	const std::deque<PhotoId> *photoIds = nullptr;
	base::optional<int> count;
};

class UserPhotos {
public:
	void add(UserPhotosAddNew &&query);
	void add(UserPhotosAddSlice &&query);
	void remove(UserPhotosRemoveOne &&query);
	void remove(UserPhotosRemoveAfter &&query);

	rpl::producer<UserPhotosResult> query(UserPhotosQuery &&query) const;
	rpl::producer<UserPhotosSliceUpdate> sliceUpdated() const;

private:
	class List {
	public:
		void addNew(PhotoId photoId);
		void addSlice(
			std::vector<PhotoId> &&photoIds,
			int count);
		void removeOne(PhotoId photoId);
		void removeAfter(PhotoId photoId);
		rpl::producer<UserPhotosResult> query(UserPhotosQuery &&query) const;

		struct SliceUpdate {
			const std::deque<PhotoId> *photoIds = nullptr;
			base::optional<int> count;
		};
		rpl::producer<SliceUpdate> sliceUpdated() const;

	private:
		void sendUpdate();

		base::optional<int> _count;
		std::deque<PhotoId> _photoIds;

		rpl::event_stream<SliceUpdate> _sliceUpdated;

	};
	using SliceUpdate = List::SliceUpdate;

	std::map<UserId, List>::iterator enforceLists(UserId user);

	std::map<UserId, List> _lists;

	rpl::event_stream<UserPhotosSliceUpdate> _sliceUpdated;

	rpl::lifetime _lifetime;

};

} // namespace Storage
