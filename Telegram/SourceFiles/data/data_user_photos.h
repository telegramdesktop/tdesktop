/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_abstract_sparse_ids.h"
#include "storage/storage_user_photos.h"
#include "base/weak_ptr.h"

namespace Main {
class Session;
} // namespace Main

class UserPhotosSlice final : public AbstractSparseIds<std::deque<PhotoId>> {
public:
	using Key = Storage::UserPhotosKey;

	UserPhotosSlice(Key key);
	UserPhotosSlice(
		Key key,
		std::deque<PhotoId> &&ids,
		std::optional<int> fullCount,
		std::optional<int> skippedBefore,
		std::optional<int> skippedAfter);

	std::optional<int> distance(const Key &a, const Key &b) const;
	const Key &key() const { return _key; }

private:
	Key _key;

	friend class UserPhotosSliceBuilder;

};

rpl::producer<UserPhotosSlice> UserPhotosViewer(
	not_null<Main::Session*> session,
	UserPhotosSlice::Key key,
	int limitBefore,
	int limitAfter);

rpl::producer<UserPhotosSlice> UserPhotosReversedViewer(
	not_null<Main::Session*> session,
	UserPhotosSlice::Key key,
	int limitBefore,
	int limitAfter);
