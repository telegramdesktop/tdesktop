/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace AdminLog {

class OwnedItem;
class LocalIdManager;

void GenerateItems(not_null<History*> history, LocalIdManager &idManager, const MTPDchannelAdminLogEvent &event, base::lambda<void(OwnedItem item)> callback);

// Smart pointer wrapper for HistoryItem* that destroys the owned item.
class OwnedItem {
public:
	explicit OwnedItem(not_null<HistoryItem*> data);
	OwnedItem(const OwnedItem &other) = delete;
	OwnedItem &operator=(const OwnedItem &other) = delete;
	OwnedItem(OwnedItem &&other);
	OwnedItem &operator=(OwnedItem &&other);
	~OwnedItem();

	HistoryView::Message *get() const {
		return _view.get();
	}
	HistoryView::Message *operator->() const {
		return get();
	}
	operator HistoryView::Message*() const {
		return get();
	}

private:
	HistoryItem *_data = nullptr;
	std::unique_ptr<HistoryView::Message> _view;

};

} // namespace AdminLog
