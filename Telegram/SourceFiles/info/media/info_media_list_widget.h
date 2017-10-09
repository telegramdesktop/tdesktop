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

namespace Ui {
class PopupMenu;
} // namespace Ui

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

using BaseLayout = Overview::Layout::ItemBase;
using UniversalMsgId = int32;

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
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	enum class MouseAction {
		None,
		PrepareDrag,
		Dragging,
		PrepareSelect,
		Selecting,
	};
	struct CachedItem {
		CachedItem(std::unique_ptr<BaseLayout> item);
		~CachedItem();

		std::unique_ptr<BaseLayout> item;
		bool stale = false;
	};
	class Section;
	struct FoundItem {
		not_null<BaseLayout*> layout;
		QRect geometry;
		bool exact = false;
	};

	void start();
	int recountHeight();
	void refreshHeight();

	QMargins padding() const;
	void updateSelected();
	bool isMyItem(not_null<const HistoryItem*> item) const;
	bool isItemLayout(
		not_null<const HistoryItem*> item,
		BaseLayout *layout) const;
	void repaintItem(const HistoryItem *item);
	void repaintItem(UniversalMsgId msgId);
	void repaintItem(const BaseLayout *item);
	void itemRemoved(not_null<const HistoryItem*> item);
	void itemLayoutChanged(not_null<const HistoryItem*> item);

	void refreshViewer();
	void invalidatePaletteCache();
	void refreshRows();
	SharedMediaMergedSlice::Key sliceKey(
		UniversalMsgId universalId) const;
	BaseLayout *getLayout(const FullMsgId &itemId);
	BaseLayout *getExistingLayout(const FullMsgId &itemId) const;
	std::unique_ptr<BaseLayout> createLayout(
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
	base::optional<FoundItem> findItemById(UniversalMsgId universalId);
	base::optional<FoundItem> findItemDetails(BaseLayout *item);
	FoundItem foundItemInSection(
		const FoundItem &item,
		const Section &section);

	void saveScrollState();
	void restoreScrollState();

	QPoint clampMousePosition(QPoint position) const;
	void mouseActionStart(
		const QPoint &screenPos,
		Qt::MouseButton button);
	void mouseActionUpdate(const QPoint &screenPos);
	void mouseActionFinish(
		const QPoint &screenPos,
		Qt::MouseButton button);
	void mouseActionCancel();
	void performDrag();

	not_null<Window::Controller*> _controller;
	not_null<PeerData*> _peer;
	Type _type = Type::Photo;

	static constexpr auto kMinimalIdsLimit = 16;
	UniversalMsgId _universalAroundId = (ServerMaxMsgId - 1);
	int _idsLimit = kMinimalIdsLimit;
	SharedMediaMergedSlice _slice;

	std::map<UniversalMsgId, CachedItem> _layouts;
	std::vector<Section> _sections;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	UniversalMsgId _scrollTopId = 0;
	int _scrollTopShift = 0;
	rpl::event_stream<int> _scrollToRequests;

	MouseAction _mouseAction = MouseAction::None;
	TextSelectType _mouseSelectType = TextSelectType::Letters;
	QPoint _dragStartPosition;
	QPoint _mousePosition;
	BaseLayout *_itemNearestToCursor = nullptr;
	BaseLayout *_itemUnderCursor = nullptr;
	BaseLayout *_itemUnderPress = nullptr;
	HistoryCursorState _mouseCursorState = HistoryDefaultCursorState;
	uint16 _mouseTextSymbol = 0;
	bool _pressWasInactive = false;
	using SelectedItems = std::map<
		UniversalMsgId,
		TextSelection,
		std::less<>>;
	SelectedItems _selected;
	style::cursor _cursor = style::cur_default;
	BaseLayout *_dragSelFrom = nullptr;
	BaseLayout *_dragSelTo = nullptr;
	bool _dragSelecting = false;
	bool _wasSelectedText = false; // was some text selected in current drag action
	Ui::PopupMenu *_contextMenu = nullptr;
	ClickHandlerPtr _contextMenuLink;

	QPoint _trippleClickPoint;
	QTimer _trippleClickTimer;

	rpl::lifetime _viewerLifetime;

};

} // namespace Media
} // namespace Info
