/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/storage_user_photos.h"
#include "base/weak_ptr.h"

class UserPhotosSlice {
public:
	using Key = Storage::UserPhotosKey;

	UserPhotosSlice(Key key);
	UserPhotosSlice(
		Key key,
		std::deque<PhotoId> &&ids,
		std::optional<int> fullCount,
		std::optional<int> skippedBefore,
		std::optional<int> skippedAfter);

	void reverse();

	const Key &key() const { return _key; }

	std::optional<int> fullCount() const { return _fullCount; }
	std::optional<int> skippedBefore() const { return _skippedBefore; }
	std::optional<int> skippedAfter() const { return _skippedAfter; }
	std::optional<int> indexOf(PhotoId msgId) const;
	int size() const { return _ids.size(); }
	PhotoId operator[](int index) const;
	std::optional<int> distance(const Key &a, const Key &b) const;

private:
	Key _key;
	std::deque<PhotoId> _ids;
	std::optional<int> _fullCount;
	std::optional<int> _skippedBefore;
	std::optional<int> _skippedAfter;

	friend class UserPhotosSliceBuilder;

};

rpl::producer<UserPhotosSlice> UserPhotosViewer(
	UserPhotosSlice::Key key,
	int limitBefore,
	int limitAfter);

rpl::producer<UserPhotosSlice> UserPhotosReversedViewer(
	UserPhotosSlice::Key key,
	int limitBefore,
	int limitAfter);
