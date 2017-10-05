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

#include "ui/rp_widget.h"
#include "info/media/info_media_widget.h"
#include "history/history_shared_media.h"

namespace Overview {
namespace Layout {
class ItemBase;
} // namespace Layout
} // namespace Overview

namespace Window {
class Controller;
} // namespace Window

namespace Info {
namespace Media {

class ListWidget : public Ui::RpWidget {
public:
	using Type = Widget::Type;
	ListWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<PeerData*> peer,
		Type type);

	not_null<Window::Controller*> controller() const {
		return _controller;
	}
	not_null<PeerData*> peer() const {
		return _peer;
	}
	Type type() const {
		return _type;
	}

	rpl::producer<int> scrollToRequests() const {
		return _scrollToRequests.events();
	}

	~ListWidget();

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintEvent(QPaintEvent *e) override;

private:
	using ItemBase = Overview::Layout::ItemBase;
	using UniversalMsgId = int32;
	struct CachedItem {
		CachedItem(std::unique_ptr<ItemBase> item);
		~CachedItem();

		std::unique_ptr<ItemBase> item;
		bool stale = false;
	};
	class Section;
	struct FoundItem {
		not_null<ItemBase*> layout;
		QRect geometry;
		bool exact = false;
	};

	void start();
	int recountHeight();
	void refreshHeight();

	QMargins padding() const;
	void updateSelected();
	bool myItem(not_null<const HistoryItem*> item) const;
	void repaintItem(not_null<const HistoryItem*> item);
	void repaintItem(UniversalMsgId msgId);
	void itemRemoved(not_null<const HistoryItem*> item);

	void refreshViewer();
	void invalidatePaletteCache();
	void refreshRows();
	SharedMediaMergedSlice::Key sliceKey(
		UniversalMsgId universalId) const;
	ItemBase *getLayout(const FullMsgId &itemId);
	std::unique_ptr<ItemBase> createLayout(
		const FullMsgId &itemId,
		Type type);

	void markLayoutsStale();
	void clearStaleLayouts();
	std::vector<Section>::iterator findSectionByItem(
		UniversalMsgId universalId);
	std::vector<Section>::iterator findSectionAfterTop(int top);
	std::vector<Section>::const_iterator findSectionAfterTop(
		int top) const;
	std::vector<Section>::const_iterator findSectionAfterBottom(
		std::vector<Section>::const_iterator from,
		int bottom) const;
	FoundItem findItemByPoint(QPoint point);
	FoundItem foundItemInSection(
		const FoundItem &item,
		const Section &section);

	void saveScrollState();
	void restoreScrollState();

	not_null<Window::Controller*> _controller;
	not_null<PeerData*> _peer;
	Type _type = Type::Photo;

	UniversalMsgId _universalAroundId = ServerMaxMsgId - 1;
	static constexpr auto kMinimalIdsLimit = 16;
	int _idsLimit = kMinimalIdsLimit;
	SharedMediaMergedSlice _slice;

	std::map<UniversalMsgId, CachedItem> _layouts;
	std::vector<Section> _sections;

	int _visibleTop = 0;
	UniversalMsgId _scrollTopId = 0;
	int _scrollTopShift = 0;
	rpl::event_stream<int> _scrollToRequests;

	rpl::lifetime _viewerLifetime;

};

} // namespace Media
} // namespace Info
