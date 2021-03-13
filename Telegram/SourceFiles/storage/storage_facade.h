/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include "base/enum_mask.h"

namespace Data {
struct MessagesResult;
} // namespace Data

namespace Storage {

struct SparseIdsListResult;

struct SharedMediaAddNew;
struct SharedMediaAddExisting;
struct SharedMediaAddSlice;
struct SharedMediaRemoveOne;
struct SharedMediaRemoveAll;
struct SharedMediaInvalidateBottom;
struct SharedMediaQuery;
struct SharedMediaKey;
using SharedMediaResult = SparseIdsListResult;
struct SharedMediaSliceUpdate;

struct UserPhotosAddNew;
struct UserPhotosAddSlice;
struct UserPhotosRemoveOne;
struct UserPhotosRemoveAfter;
struct UserPhotosQuery;
struct UserPhotosResult;
struct UserPhotosSliceUpdate;

class Facade {
public:
	Facade();

	void add(SharedMediaAddNew &&query);
	void add(SharedMediaAddExisting &&query);
	void add(SharedMediaAddSlice &&query);
	void remove(SharedMediaRemoveOne &&query);
	void remove(SharedMediaRemoveAll &&query);
	void invalidate(SharedMediaInvalidateBottom &&query);

	rpl::producer<SharedMediaResult> query(SharedMediaQuery &&query) const;
	SharedMediaResult snapshot(const SharedMediaQuery &query) const;
	bool empty(const SharedMediaKey &key) const;
	rpl::producer<SharedMediaSliceUpdate> sharedMediaSliceUpdated() const;
	rpl::producer<SharedMediaRemoveOne> sharedMediaOneRemoved() const;
	rpl::producer<SharedMediaRemoveAll> sharedMediaAllRemoved() const;
	rpl::producer<SharedMediaInvalidateBottom> sharedMediaBottomInvalidated() const;

	void add(UserPhotosAddNew &&query);
	void add(UserPhotosAddSlice &&query);
	void remove(UserPhotosRemoveOne &&query);
	void remove(UserPhotosRemoveAfter &&query);

	rpl::producer<UserPhotosResult> query(UserPhotosQuery &&query) const;
	rpl::producer<UserPhotosSliceUpdate> userPhotosSliceUpdated() const;

	~Facade();

private:
	class Impl;
	const std::unique_ptr<Impl> _impl;

};

} // namespace Storage
