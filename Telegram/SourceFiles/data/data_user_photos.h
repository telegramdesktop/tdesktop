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
		base::optional<int> fullCount,
		base::optional<int> skippedBefore,
		base::optional<int> skippedAfter);

	void reverse();

	const Key &key() const { return _key; }

	base::optional<int> fullCount() const { return _fullCount; }
	base::optional<int> skippedBefore() const { return _skippedBefore; }
	base::optional<int> skippedAfter() const { return _skippedAfter; }
	base::optional<int> indexOf(PhotoId msgId) const;
	int size() const { return _ids.size(); }
	PhotoId operator[](int index) const;
	base::optional<int> distance(const Key &a, const Key &b) const;

private:
	Key _key;
	std::deque<PhotoId> _ids;
	base::optional<int> _fullCount;
	base::optional<int> _skippedBefore;
	base::optional<int> _skippedAfter;

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
