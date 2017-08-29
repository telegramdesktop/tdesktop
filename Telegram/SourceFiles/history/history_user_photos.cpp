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
#include "history/history_user_photos.h"

#include "auth_session.h"
#include "apiwrap.h"
#include "storage/storage_facade.h"
#include "storage/storage_user_photos.h"

UserPhotosSlice::UserPhotosSlice(Key key) : UserPhotosSlice(key, base::none) {
}

UserPhotosSlice::UserPhotosSlice(
	Key key,
	base::optional<int> fullCount)
	: _key(key)
	, _fullCount(fullCount) {
}

base::optional<int> UserPhotosSlice::indexOf(PhotoId photoId) const {
	auto it = base::find(_ids, photoId);
	if (it != _ids.end()) {
		return (it - _ids.begin());
	}
	return base::none;
}

PhotoId UserPhotosSlice::operator[](int index) const {
	Expects(index >= 0 && index < size());

	return *(_ids.begin() + index);
}

base::optional<int> UserPhotosSlice::distance(const Key &a, const Key &b) const {
	if (a.userId != _key.userId
		|| b.userId != _key.userId) {
		return base::none;
	}
	if (auto i = indexOf(a.photoId)) {
		if (auto j = indexOf(b.photoId)) {
			return *j - *i;
		}
	}
	return base::none;
}

QString UserPhotosSlice::debug() const {
	auto before = _skippedBefore
		? (*_skippedBefore
			? ('(' + QString::number(*_skippedBefore) + ").. ")
			: QString())
		: QString(".. ");
	auto after = _skippedAfter
		? (" ..(" + QString::number(_skippedAfter) + ')')
		: QString(" ..");
	auto middle = (size() > 2)
		? QString::number((*this)[0]) + " .. " + QString::number((*this)[size() - 1])
		: (size() > 1)
		? QString::number((*this)[0]) + ' ' + QString::number((*this)[1])
		: ((size() > 0) ? QString::number((*this)[0]) : QString());
	return before + middle + after;
}

UserPhotosViewer::UserPhotosViewer(
	Key key,
	int limitBefore,
	int limitAfter)
	: _key(key)
	, _limitBefore(limitBefore)
	, _limitAfter(limitAfter)
	, _data(_key) {
}

void UserPhotosViewer::start() {
	auto applyUpdateCallback = [this](auto &update) {
		this->applyUpdate(update);
	};
	subscribe(Auth().storage().userPhotosSliceUpdated(), applyUpdateCallback);

	loadInitial();
}

void UserPhotosViewer::loadInitial() {
	auto weak = base::make_weak_unique(this);
	Auth().storage().query(Storage::UserPhotosQuery(
		_key,
		_limitBefore,
		_limitAfter), [weak](Storage::UserPhotosResult &&result) {
		if (weak) {
			weak->applyStoredResult(std::move(result));
		}
	});
}

void UserPhotosViewer::applyStoredResult(Storage::UserPhotosResult &&result) {
	mergeSliceData(
		result.count,
		result.photoIds,
		result.skippedBefore,
		result.skippedAfter);
}

void UserPhotosViewer::mergeSliceData(
		base::optional<int> count,
		const std::deque<PhotoId> &photoIds,
		base::optional<int> skippedBefore,
		int skippedAfter) {
	if (photoIds.empty()) {
		if (_data._fullCount != count) {
			_data._fullCount = count;
			if (_data._fullCount && *_data._fullCount <= _data.size()) {
				_data._fullCount = _data.size();
				_data._skippedBefore = _data._skippedAfter = 0;
			}
			updated.notify(_data);
		}
		sliceToLimits();
		return;
	}
	if (count) {
		_data._fullCount = count;
	}
	_data._skippedAfter = skippedAfter;
	_data._ids = photoIds;

	if (_data._fullCount) {
		_data._skippedBefore = *_data._fullCount
			- _data._skippedAfter
			- int(_data._ids.size());
	}

	sliceToLimits();

	updated.notify(_data);
}

void UserPhotosViewer::applyUpdate(const SliceUpdate &update) {
	if (update.userId != _key.userId) {
		return;
	}
	auto idsCount = update.photoIds ? int(update.photoIds->size()) : 0;
	mergeSliceData(
		update.count,
		update.photoIds ? *update.photoIds : std::deque<PhotoId> {},
		update.count | func::add(-idsCount),
		0);
}

void UserPhotosViewer::sliceToLimits() {
	auto aroundIt = base::find(_data._ids, _key.photoId);
	auto removeFromBegin = (aroundIt - _data._ids.begin() - _limitBefore);
	auto removeFromEnd = (_data._ids.end() - aroundIt - _limitAfter - 1);
	if (removeFromEnd > 0) {
		_data._ids.erase(_data._ids.end() - removeFromEnd, _data._ids.end());
		_data._skippedAfter += removeFromEnd;
	}
	if (removeFromBegin > 0) {
		_data._ids.erase(_data._ids.begin(), _data._ids.begin() + removeFromBegin);
		if (_data._skippedBefore) {
			*_data._skippedBefore += removeFromBegin;
		}
	} else if (removeFromBegin < 0 && (!_data._skippedBefore || *_data._skippedBefore > 0)) {
		requestPhotos();
	}
}

void UserPhotosViewer::requestPhotos() {
	Auth().api().requestUserPhotos(
		App::user(_key.userId),
		_data._ids.empty() ? 0 : _data._ids.front());
}
