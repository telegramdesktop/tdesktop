/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

	UserId userId = 0;
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
	std::optional<int> count;
	std::optional<int> skippedBefore;
	int skippedAfter = 0;
	std::deque<PhotoId> photoIds;
};

struct UserPhotosSliceUpdate {
	UserPhotosSliceUpdate(
		UserId userId,
		const std::deque<PhotoId> *photoIds,
		std::optional<int> count)
		: userId(userId)
		, photoIds(photoIds)
		, count(count) {
	}

	UserId userId = 0;
	const std::deque<PhotoId> *photoIds = nullptr;
	std::optional<int> count;
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
			std::optional<int> count;
		};
		rpl::producer<SliceUpdate> sliceUpdated() const;

	private:
		void sendUpdate();

		std::optional<int> _count;
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
