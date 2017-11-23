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

#include "storage/storage_user_photos.h"
#include "base/weak_unique_ptr.h"

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
