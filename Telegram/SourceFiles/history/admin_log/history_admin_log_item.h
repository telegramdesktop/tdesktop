/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
