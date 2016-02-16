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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

class OverviewWidget;
class OverviewInner : public QWidget, public AbstractTooltipShower, public RPCSender {
	Q_OBJECT

public:

	OverviewInner(OverviewWidget *overview, ScrollArea *scroll, PeerData *peer, MediaOverviewType type);

	void activate();

	void clear();
	int32 itemTop(const FullMsgId &msgId) const;

	bool preloadLocal();
	void preloadMore();

	bool event(QEvent *e);
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void resizeEvent(QResizeEvent *e);

	void showContextMenu(QContextMenuEvent *e, bool showFromTouch = false);

	void dragActionStart(const QPoint &screenPos, Qt::MouseButton button = Qt::LeftButton);
	void dragActionUpdate(const QPoint &screenPos);
	void dragActionFinish(const QPoint &screenPos, Qt::MouseButton button = Qt::LeftButton);
	void dragActionCancel();

	void touchScrollUpdated(const QPoint &screenPos);
	QPoint mapMouseToItem(QPoint p, MsgId itemId, int32 itemIndex);

	int32 resizeToWidth(int32 nwidth, int32 scrollTop, int32 minHeight, bool force = false); // returns new scroll top
	void dropResizeIndex();

	PeerData *peer() const;
	PeerData *migratePeer() const;
	MediaOverviewType type() const;
	void switchType(MediaOverviewType type);

	void setSelectMode(bool enabled);

	void mediaOverviewUpdated();
	void changingMsgId(HistoryItem *row, MsgId newId);
	void repaintItem(const HistoryItem *msg);
	void itemRemoved(HistoryItem *item);
	
	void getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const;
	void clearSelectedItems(bool onlyTextSelection = false);
	void fillSelectedItems(SelectedItemSet &sel, bool forDelete = true);

	// AbstractTooltipShower
	virtual QString tooltipText() const;
	virtual QPoint tooltipPos() const;

	~OverviewInner();

public slots:

	void onUpdateSelected();

	void openContextUrl();
	void copyContextUrl();
	void cancelContextDownload();
	void showContextInFolder();
	void saveContextFile();
	void openContextFile();

	void goToMessage();
	void deleteMessage();
	void forwardMessage();
	void selectMessage();

	void onSearchUpdate();
	void onCancel();
	bool onCancelSearch();

	void onMenuDestroy(QObject *obj);
	void onTouchSelect();
	void onTouchScrollTimer();

	void onDragExec();

	bool onSearchMessages(bool searchCache = false);
	void onNeedSearchMessages();

private:

	MsgId complexMsgId(const HistoryItem *item) const;

	bool itemMigrated(MsgId msgId) const;
	ChannelId itemChannel(MsgId msgId) const;
	MsgId itemMsgId(MsgId msgId) const;
	int32 migratedIndexSkip() const;

	void fixItemIndex(int32 &current, MsgId msgId) const;
	bool itemHasPoint(MsgId msgId, int32 index, int32 x, int32 y) const;
	int32 itemHeight(MsgId msgId, int32 index) const;
	void moveToNextItem(MsgId &msgId, int32 &index, MsgId upTo, int32 delta) const;

	void updateDragSelection(MsgId dragSelFrom, int32 dragSelFromIndex, MsgId dragSelTo, int32 dragSelToIndex, bool dragSelecting);

	void repaintItem(MsgId itemId, int32 itemIndex);

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	void applyDragSelection();
	void addSelectionRange(int32 selFrom, int32 selTo, History *history);

	void recountMargins();
	int32 countHeight();

	OverviewWidget *_overview;
	ScrollArea *_scroll;
	int32 _resizeIndex, _resizeSkip;

	PeerData *_peer;
	MediaOverviewType _type;
	bool _reversed;
	History *_migrated, *_history;
	ChannelId _channel;
	
	bool _selMode;
	uint32 itemSelectedValue(int32 index) const;

	int32 _rowsLeft, _rowWidth;

	typedef QVector<LayoutItem*> Items;
	Items _items;
	typedef QMap<HistoryItem*, LayoutMediaItem*> LayoutItems;
	LayoutItems _layoutItems;
	typedef QMap<int32, LayoutOverviewDate*> LayoutDates;
	LayoutDates _layoutDates;
	LayoutMediaItem *layoutPrepare(HistoryItem *item);
	LayoutItem *layoutPrepare(const QDate &date, bool month);
	int32 setLayoutItem(int32 index, LayoutItem *item, int32 top);

	FlatInput _search;
	IconedButton _cancelSearch;
	QVector<MsgId> _results;
	int32 _itemsToBeLoaded;

	// photos
	int32 _photosInRow, _photosToAdd;

	QTimer _searchTimer;
	QString _searchQuery;
	bool _inSearch, _searchFull, _searchFullMigrated;
	mtpRequestId _searchRequest;
	History::MediaOverview _searchResults;
	MsgId _lastSearchId, _lastSearchMigratedId;
	int32 _searchedCount;
	enum SearchRequestType {
		SearchFromStart,
		SearchFromOffset,
		SearchMigratedFromStart,
		SearchMigratedFromOffset
	};
	void searchReceived(SearchRequestType type, const MTPmessages_Messages &result, mtpRequestId req);
	bool searchFailed(SearchRequestType type, const RPCError &error, mtpRequestId req);

	typedef QMap<QString, MTPmessages_Messages> SearchCache;
	SearchCache _searchCache;

	typedef QMap<mtpRequestId, QString> SearchQueries;
	SearchQueries _searchQueries;

	int32 _width, _height, _minHeight, _marginTop, _marginBottom;

	QTimer _linkTipTimer;

	// selection support, like in HistoryWidget
	Qt::CursorShape _cursor;
	HistoryCursorState _cursorState;
	typedef QMap<MsgId, uint32> SelectedItems;
	SelectedItems _selected;
	enum DragAction {
		NoDrag = 0x00,
		PrepareDrag = 0x01,
		Dragging = 0x02,
		PrepareSelect = 0x03,
		Selecting = 0x04,
	};
	DragAction _dragAction;
	QPoint _dragStartPos, _dragPos;
	MsgId _dragItem, _selectedMsgId;
	int32 _dragItemIndex;
	MsgId _mousedItem;
	int32 _mousedItemIndex;
	uint16 _dragSymbol;
	bool _dragWasInactive;

	TextLinkPtr _contextMenuLnk;

	MsgId _dragSelFrom, _dragSelTo;
	int32 _dragSelFromIndex, _dragSelToIndex;
	bool _dragSelecting;

	bool _touchScroll, _touchSelect, _touchInProgress;
	QPoint _touchStart, _touchPrevPos, _touchPos;
	QTimer _touchSelectTimer;

	TouchScrollState _touchScrollState;
	bool _touchPrevPosValid, _touchWaitingAcceleration;
	QPoint _touchSpeed;
	uint64 _touchSpeedTime, _touchAccelerationTime, _touchTime;
	QTimer _touchScrollTimer;

	PopupMenu *_menu;
};

class OverviewWidget : public TWidget, public RPCSender {
	Q_OBJECT

public:

	OverviewWidget(QWidget *parent, PeerData *peer, MediaOverviewType type);

	void clear();

	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);

	void scrollBy(int32 add);
	void scrollReset();

	void paintTopBar(QPainter &p, float64 over, int32 decreaseWidth);
	void topBarClick();

	PeerData *peer() const;
	PeerData *migratePeer() const;
	MediaOverviewType type() const;
	void switchType(MediaOverviewType type);
	void updateTopBarSelection();

	int32 lastWidth() const;
	int32 lastScrollTop() const;
	int32 countBestScroll() const;

	void fastShow(bool back = false, int32 lastScrollTop = -1);
	void animShow(const QPixmap &oldAnimCache, const QPixmap &bgAnimTopBarCache, bool back = false, int32 lastScrollTop = -1);
	void step_show(float64 ms, bool timer);

	void updateAdaptiveLayout();
	void doneShow();

	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void itemRemoved(HistoryItem *item);
	
	QPoint clampMousePosition(QPoint point);

	void checkSelectingScroll(QPoint point);
	void noSelectingScroll();

	bool touchScroll(const QPoint &delta);
	
	void fillSelectedItems(SelectedItemSet &sel, bool forDelete);

	void updateScrollColors();

	void updateAfterDrag();

	void grabStart() {
		_sideShadow.hide();
		_inGrab = true;
		resizeEvent(0);
	}
	void grabFinish() {
		_sideShadow.setVisible(!Adaptive::OneColumn());
		_inGrab = false;
		resizeEvent(0);
	}

	void ui_repaintHistoryItem(const HistoryItem *item);

	void notify_historyItemLayoutChanged(const HistoryItem *item);

	~OverviewWidget();

public slots:

	void activate();
	void onScroll();

	void onScrollTimer();
	void onPlayerSongChanged(const FullMsgId &msgId);

	void onForwardSelected();
	void onDeleteSelected();
	void onDeleteSelectedSure();
	void onDeleteContextSure();
	void onClearSelected();

private:

	ScrollArea _scroll;
	OverviewInner _inner;
	bool _noDropResizeIndex;

	QString _header;

	Animation _a_show;
	QPixmap _cacheUnder, _cacheOver, _cacheTopBarUnder, _cacheTopBarOver;
	anim::ivalue a_coordUnder, a_coordOver;
	anim::fvalue a_shadow;

	int32 _scrollSetAfterShow;

	QTimer _scrollTimer;
	int32 _scrollDelta;

	int32 _selCount;

	PlainShadow _sideShadow, _topShadow;
	bool _inGrab;

};

