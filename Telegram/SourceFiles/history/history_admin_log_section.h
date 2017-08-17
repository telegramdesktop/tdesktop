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

#include "window/section_widget.h"
#include "window/section_memento.h"
#include "history/history_admin_log_item.h"
#include "mtproto/sender.h"

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Ui {
class ScrollArea;
class PlainShadow;
class FlatButton;
} // namespace Ui

namespace Profile {
class BackButton;
} // namespace Profile

namespace AdminLog {

class FixedBar;
class InnerWidget;
class SectionMemento;

struct FilterValue {
	// Empty "flags" means all events.
	MTPDchannelAdminLogEventsFilter::Flags flags = 0;
	std::vector<not_null<UserData*>> admins;
	bool allUsers = true;
};

inline bool operator==(const FilterValue &a, const FilterValue &b) {
	return (a.flags == b.flags && a.admins == b.admins && a.allUsers == b.allUsers);
}

inline bool operator!=(const FilterValue &a, const FilterValue &b) {
	return !(a == b);
}

class LocalIdManager {
public:
	LocalIdManager() = default;
	LocalIdManager(const LocalIdManager &other) = delete;
	LocalIdManager &operator=(const LocalIdManager &other) = delete;
	LocalIdManager(LocalIdManager &&other) : _counter(std::exchange(other._counter, ServerMaxMsgId)) {
	}
	LocalIdManager &operator=(LocalIdManager &&other) {
		_counter = std::exchange(other._counter, ServerMaxMsgId);
		return *this;
	}
	MsgId next() {
		return ++_counter;
	}

private:
	MsgId _counter = ServerMaxMsgId;

};

class Widget final : public Window::SectionWidget {
public:
	Widget(QWidget *parent, not_null<Window::Controller*> controller, not_null<ChannelData*> channel);

	not_null<ChannelData*> channel() const;
	PeerData *peerForDialogs() const override {
		return channel();
	}

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params) override;

	bool showInternal(not_null<Window::SectionMemento*> memento) override;
	std::unique_ptr<Window::SectionMemento> createMemento() override;

	void setInternalState(const QRect &geometry, not_null<SectionMemento*> memento);

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) override;
	QRect rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) override;

	void applyFilter(FilterValue &&value);

	bool cmd_search() override;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void showAnimatedHook() override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;

private:
	void showFilter();
	void onScroll();
	void updateAdaptiveLayout();
	void saveState(not_null<SectionMemento*> memento);
	void restoreState(not_null<SectionMemento*> memento);

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<InnerWidget> _inner;
	object_ptr<FixedBar> _fixedBar;
	object_ptr<Ui::PlainShadow> _fixedBarShadow;
	object_ptr<Ui::FlatButton> _whatIsThis;

};

class SectionMemento : public Window::SectionMemento {
public:
	SectionMemento(not_null<ChannelData*> channel) : _channel(channel) {
	}

	object_ptr<Window::SectionWidget> createWidget(QWidget *parent, not_null<Window::Controller*> controller, const QRect &geometry) override;

	not_null<ChannelData*> getChannel() const {
		return _channel;
	}
	void setScrollTop(int scrollTop) {
		_scrollTop = scrollTop;
	}
	int getScrollTop() const {
		return _scrollTop;
	}

	void setAdmins(std::vector<not_null<UserData*>> admins) {
		_admins = std::move(admins);
	}
	void setAdminsCanEdit(std::vector<not_null<UserData*>> admins) {
		_adminsCanEdit = std::move(admins);
	}
	std::vector<not_null<UserData*>> takeAdmins() {
		return std::move(_admins);
	}
	std::vector<not_null<UserData*>> takeAdminsCanEdit() {
		return std::move(_adminsCanEdit);
	}

	void setItems(std::vector<HistoryItemOwned> &&items, std::map<uint64, HistoryItem*> &&itemsByIds, bool upLoaded, bool downLoaded) {
		_items = std::move(items);
		_itemsByIds = std::move(itemsByIds);
		_upLoaded = upLoaded;
		_downLoaded = downLoaded;
	}
	void setFilter(FilterValue &&filter) {
		_filter = std::move(filter);
	}
	void setSearchQuery(QString &&query) {
		_searchQuery = std::move(query);
	}
	void setIdManager(LocalIdManager &&manager) {
		_idManager = std::move(manager);
	}
	std::vector<HistoryItemOwned> takeItems() {
		return std::move(_items);
	}
	std::map<uint64, HistoryItem*> takeItemsByIds() {
		return std::move(_itemsByIds);
	}
	LocalIdManager takeIdManager() {
		return std::move(_idManager);
	}
	bool upLoaded() const {
		return _upLoaded;
	}
	bool downLoaded() const {
		return _downLoaded;
	}
	FilterValue takeFilter() {
		return std::move(_filter);
	}
	QString takeSearchQuery() {
		return std::move(_searchQuery);
	}

private:
	not_null<ChannelData*> _channel;
	int _scrollTop = 0;
	std::vector<not_null<UserData*>> _admins;
	std::vector<not_null<UserData*>> _adminsCanEdit;
	std::vector<HistoryItemOwned> _items;
	std::map<uint64, HistoryItem*> _itemsByIds;
	bool _upLoaded = false;
	bool _downLoaded = true;
	LocalIdManager _idManager;
	FilterValue _filter;
	QString _searchQuery;

};

} // namespace AdminLog
