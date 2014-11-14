/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

class OverviewWidget;
class OverviewInner : public QWidget, public RPCSender {
	Q_OBJECT

public:

	OverviewInner(OverviewWidget *overview, ScrollArea *scroll, const PeerData *peer, MediaOverviewType type);

	void clear();

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

	void mediaOverviewUpdated();
	void changingMsgId(HistoryItem *row, MsgId newId);
	void msgUpdated(const HistoryItem *msg);
	void itemRemoved(HistoryItem *item);
	void itemResized(HistoryItem *item);

	void getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const;
	void clearSelectedItems(bool onlyTextSelection = false);
	void fillSelectedItems(SelectedItemSet &sel, bool forDelete = true);

	~OverviewInner();

public slots:

	void onUpdateSelected();

	void openContextUrl();
	void cancelContextDownload();
	void showContextInFolder();
	void saveContextFile();
	void openContextFile();

	void goToMessage();
	void deleteMessage();
	void forwardMessage();
	void selectMessage();

	void onMenuDestroy(QObject *obj);
	void onTouchSelect();
	void onTouchScrollTimer();

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
	void showAll();

	OverviewWidget *_overview;
	ScrollArea *_scroll;
	int32 _resizeIndex, _resizeSkip;

	PeerData *_peer;
	MediaOverviewType _type;
	History *_hist;
	
	// photos
	int32 _photosInRow, _photosToAdd, _vsize;
	typedef struct {
		int32 vsize;
		bool medium;
		QPixmap pix;
	} CachedSize;
	typedef QMap<PhotoData*, CachedSize> CachedSizes;
	CachedSizes _cached;
	bool _selMode;

	// other
	typedef struct _CachedItem {
		_CachedItem() : msgid(0), y(0) {
		}
		_CachedItem(MsgId msgid, const QDate &date, int32 y) : msgid(msgid), date(date), y(y) {
		}
		MsgId msgid;
		QDate date;
		int32 y;
	} CachedItem;
	typedef QVector<CachedItem> CachedItems;
	CachedItems _items;
	int32 _width, _height, _minHeight, _addToY;

	// selection support, like in HistoryWidget
	Qt::CursorShape _cursor;
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
	MsgId _dragItem;
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

	void paintTopBar(QPainter &p, float64 over, int32 decreaseWidth);
	void topBarClick();

	PeerData *peer() const;
	MediaOverviewType type() const;
	void switchType(MediaOverviewType type);
	void updateTopBarSelection();

	int32 lastWidth() const;
	int32 lastScrollTop() const;

	void animShow(const QPixmap &oldAnimCache, const QPixmap &bgAnimTopBarCache, bool back = false, int32 lastScrollTop = -1);
	bool animStep(float64 ms);

	void mediaOverviewUpdated(PeerData *peer);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void msgUpdated(PeerId peer, const HistoryItem *msg);
	void itemRemoved(HistoryItem *item);
	void itemResized(HistoryItem *row);

	QPoint clampMousePosition(QPoint point);

	void checkSelectingScroll(QPoint point);
	void noSelectingScroll();

	bool touchScroll(const QPoint &delta);
	
	void fillSelectedItems(SelectedItemSet &sel, bool forDelete);

	~OverviewWidget();

public slots:

	void activate();
	void onScroll();

	void onScrollTimer();

	void onForwardSelected();
	void onDeleteSelected();
	void onDeleteSelectedSure();
	void onDeleteContextSure();
	void onClearSelected();

private:

	ScrollArea _scroll;
	OverviewInner _inner;
	bool _noDropResizeIndex;

	QPixmap _bg;

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

