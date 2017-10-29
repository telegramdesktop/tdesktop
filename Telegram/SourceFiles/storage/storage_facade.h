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

#include <rpl/producer.h>
#include "base/enum_mask.h"

namespace Storage {

struct SparseIdsListResult;

struct SharedMediaAddNew;
struct SharedMediaAddExisting;
struct SharedMediaAddSlice;
struct SharedMediaRemoveOne;
struct SharedMediaRemoveAll;
struct SharedMediaQuery;
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

	rpl::producer<SharedMediaResult> query(SharedMediaQuery &&query) const;
	rpl::producer<SharedMediaSliceUpdate> sharedMediaSliceUpdated() const;
	rpl::producer<SharedMediaRemoveOne> sharedMediaOneRemoved() const;
	rpl::producer<SharedMediaRemoveAll> sharedMediaAllRemoved() const;

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
