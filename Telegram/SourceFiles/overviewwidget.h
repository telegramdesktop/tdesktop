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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

class OverviewWidget;
class OverviewInner : public QWidget, public RPCSender {
	Q_OBJECT

public:

	OverviewInner(OverviewWidget *overview, ScrollArea *scroll, const PeerData *peer, MediaOverviewType type);

	void activate();

	void clear();
	int32 itemTop(MsgId msgId) const;

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

	int32 resizeToWidth(int32 nwidth, int32 scrollTop, int32 minHeight); // returns new scroll top
	void dropResizeIndex();

	PeerData *peer() const;
	MediaOverviewType type() const;
	void switchType(MediaOverviewType type);

	void setSelectMode(bool enabled);

	void mediaOverviewUpdated(bool fromResize = false);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void msgUpdated(const HistoryItem *msg);
	void itemRemoved(HistoryItem *item);
	void itemResized(HistoryItem *item, bool scrollToIt);

	void getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const;
	void clearSelectedItems(bool onlyTextSelection = false);
	void fillSelectedItems(SelectedItemSet &sel, bool forDelete = true);

	~OverviewInner();

public slots:

	void onUpdateSelected();
	void showLinkTip();

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

	void fixItemIndex(int32 &current, MsgId msgId) const;
	bool itemHasPoint(MsgId msgId, int32 index, int32 x, int32 y) const;
	int32 itemHeight(MsgId msgId, int32 index) const;
	void moveToNextItem(MsgId &msgId, int32 &index, MsgId upTo, int32 delta) const;

	void updateDragSelection(MsgId dragSelFrom, int32 dragSelFromIndex, MsgId dragSelTo, int32 dragSelToIndex, bool dragSelecting);

	void updateMsg(HistoryItem *item);
	void updateMsg(MsgId itemId, int32 itemIndex);

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	void applyDragSelection();

	QPixmap genPix(PhotoData *photo, int32 size);
	void showAll(bool recountHeights = false);

	OverviewWidget *_overview;
	ScrollArea *_scroll;
	int32 _resizeIndex, _resizeSkip;

	PeerData *_peer;
	MediaOverviewType _type;
	History *_hist;
	
	// photos
	int32 _photosInRow, _photosToAdd, _vsize;
	struct CachedSize {
		int32 vsize;
		bool medium;
		QPixmap pix;
	};
	typedef QMap<PhotoData*, CachedSize> CachedSizes;
	CachedSizes _cached;
	bool _selMode;

	// audio documents
	int32 _audioLeft, _audioWidth, _audioHeight;

	// shared links
	int32 _linksLeft, _linksWidth;
	struct Link {
		Link() : width(0) {
		}
		Link(const QString &url, const QString &text) : url(url), text(text), width(st::msgFont->m.width(text)) {
		}
		QString url, text;
		int32 width;
	};
	struct CachedLink {
		CachedLink() : titleWidth(0), page(0), pixw(0), pixh(0), text(st::msgMinWidth) {
		}
		CachedLink(HistoryItem *item);
		int32 countHeight(int32 w);

		QString title, letter;
		int32 titleWidth;
		WebPageData *page;
		int32 pixw, pixh;
		Text text;
		QVector<Link> urls;
	};
	typedef QMap<MsgId, CachedLink*> CachedLinks;
	CachedLinks _links;
	FlatInput _search;
	IconedButton _cancelSearch;
	QVector<MsgId> _results;
	int32 _itemsToBeLoaded;

	QTimer _searchTimer;
	QString _searchQuery;
	bool _inSearch, _searchFull;
	mtpRequestId _searchRequest;
	History::MediaOverview _searchResults;
	MsgId _lastSearchId;
	int32 _searchedCount;
	void searchReceived(bool fromStart, const MTPmessages_Messages &result, mtpRequestId req);
	bool searchFailed(const RPCError &error, mtpRequestId req);

	typedef QMap<QString, MTPmessages_Messages> SearchCache;
	SearchCache _searchCache;

	typedef QMap<mtpRequestId, QString> SearchQueries;
	SearchQueries _searchQueries;

	CachedLink *cachedLink(HistoryItem *item);

	// other
	struct CachedItem {
		CachedItem() : msgid(0), y(0) {
		}
		CachedItem(MsgId msgid, const QDate &date, int32 y) : msgid(msgid), date(date), y(y) {
		}
		MsgId msgid;
		QDate date;
		int32 y;
		CachedLink *link;
	};
	typedef QVector<CachedItem> CachedItems;
	CachedItems _items;

	int32 _width, _height, _minHeight, _addToY;

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
	int32 _lnkOverIndex, _lnkDownIndex; // for OverviewLinks, 0 - none, -1 - photo or title, > 0 - lnk index
	uint16 _dragSymbol;
	bool _dragWasInactive;

	QString urlByIndex(MsgId msgid, int32 index, int32 lnkIndex, bool *fullShown = 0) const;
	bool urlIsEmail(const QString &url) const;

	QString _contextMenuUrl;
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

	ContextMenu *_menu;
};

class OverviewWidget : public QWidget, public RPCSender, public Animated {
	Q_OBJECT

public:

	OverviewWidget(QWidget *parent, const PeerData *peer, MediaOverviewType type);

	void clear();

	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);

	void scrollBy(int32 add);
	void scrollReset();

	void paintTopBar(QPainter &p, float64 over, int32 decreaseWidth);
	void topBarShadowParams(int32 &x, float64 &o);
	void topBarClick();

	PeerData *peer() const;
	MediaOverviewType type() const;
	void switchType(MediaOverviewType type);
	void updateTopBarSelection();

	int32 lastWidth() const;
	int32 lastScrollTop() const;
	int32 countBestScroll() const;

	void fastShow(bool back = false, int32 lastScrollTop = -1);
	void animShow(const QPixmap &oldAnimCache, const QPixmap &bgAnimTopBarCache, bool back = false, int32 lastScrollTop = -1);
	bool animStep(float64 ms);

	void doneShow();

	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void msgUpdated(PeerId peer, const HistoryItem *msg);
	void itemRemoved(HistoryItem *item);
	void itemResized(HistoryItem *row, bool scrollToIt);

	QPoint clampMousePosition(QPoint point);

	void checkSelectingScroll(QPoint point);
	void noSelectingScroll();

	bool touchScroll(const QPoint &delta);
	
	void fillSelectedItems(SelectedItemSet &sel, bool forDelete);

	void updateScrollColors();

	~OverviewWidget();

public slots:

	void activate();
	void onScroll();

	void onScrollTimer();
	void onPlayerSongChanged(MsgId msgId);

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

	bool _showing;
	QPixmap _animCache, _bgAnimCache, _animTopBarCache, _bgAnimTopBarCache;
	anim::ivalue a_coord, a_bgCoord;
	anim::fvalue a_alpha, a_bgAlpha;

	int32 _scrollSetAfterShow;

	QTimer _scrollTimer;
	int32 _scrollDelta;

	int32 _selCount;

};

