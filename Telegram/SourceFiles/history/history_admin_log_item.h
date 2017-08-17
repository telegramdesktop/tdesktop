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

namespace AdminLog {

class HistoryItemOwned;
class LocalIdManager;

void GenerateItems(not_null<History*> history, LocalIdManager &idManager, const MTPDchannelAdminLogEvent &event, base::lambda<void(HistoryItemOwned item)> callback);

// Smart pointer wrapper for HistoryItem* that destroys the owned item.
class HistoryItemOwned {
public:
	explicit HistoryItemOwned(not_null<HistoryItem*> data) : _data(data) {
	}
	HistoryItemOwned(const HistoryItemOwned &other) = delete;
	HistoryItemOwned &operator=(const HistoryItemOwned &other) = delete;
	HistoryItemOwned(HistoryItemOwned &&other) : _data(base::take(other._data)) {
	}
	HistoryItemOwned &operator=(HistoryItemOwned &&other) {
		_data = base::take(other._data);
		return *this;
	}
	~HistoryItemOwned() {
		if (_data) {
			_data->destroy();
		}
	}

	HistoryItem *get() const {
		return _data;
	}
	HistoryItem *operator->() const {
		return get();
	}
	operator HistoryItem*() const {
		return get();
	}

private:
	HistoryItem *_data = nullptr;

};

} // namespace AdminLog
