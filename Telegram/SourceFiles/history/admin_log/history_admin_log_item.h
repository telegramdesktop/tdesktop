/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace HistoryView {
class ElementDelegate;
class Element;
} // namespace HistoryView

namespace AdminLog {

class OwnedItem;

void GenerateItems(
	not_null<HistoryView::ElementDelegate*> delegate,
	not_null<History*> history,
	const MTPDchannelAdminLogEvent &event,
	Fn<void(OwnedItem item, TimeId sentDate)> callback);

// Smart pointer wrapper for HistoryItem* that destroys the owned item.
class OwnedItem {
public:
	OwnedItem(std::nullptr_t = nullptr);
	OwnedItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<HistoryItem*> data);
	OwnedItem(const OwnedItem &other) = delete;
	OwnedItem &operator=(const OwnedItem &other) = delete;
	OwnedItem(OwnedItem &&other);
	OwnedItem &operator=(OwnedItem &&other);
	~OwnedItem();

	[[nodiscard]] HistoryView::Element *get() const {
		return _view.get();
	}
	[[nodiscard]] HistoryView::Element *operator->() const {
		return get();
	}
	[[nodiscard]] operator HistoryView::Element*() const {
		return get();
	}

	void refreshView(not_null<HistoryView::ElementDelegate*> delegate);
	void clearView();

private:
	HistoryItem *_data = nullptr;
	std::unique_ptr<HistoryView::Element> _view;

};

} // namespace AdminLog
