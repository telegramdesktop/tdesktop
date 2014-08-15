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
class OverviewInner : public TWidget, public RPCSender {
	Q_OBJECT

public:

	OverviewInner(OverviewWidget *overview, ScrollArea *scroll, const PeerData *peer, MediaOverviewType type);

	void clear();

	bool event(QEvent *e);
	void paintEvent(QPaintEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void leaveToChildEvent(QEvent *e);
	void resizeEvent(QResizeEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);

	int32 resizeToWidth(int32 nwidth, int32 scrollTop, int32 minHeight); // returns new scroll top
	void dropResizeIndex();

	PeerData *peer() const;
	MediaOverviewType type() const;
	void switchType(MediaOverviewType type);

	void mediaOverviewUpdated();
	void msgUpdated(HistoryItem *msg);

	~OverviewInner();

public slots:

	void updateSelected();

	void openContextUrl();
	void cancelContextDownload();
	void showContextInFolder();
	void saveContextFile();
	void openContextFile();

	void goToMessage();
	void deleteMessage();
	void forwardMessage();

	void onMenuDestroy(QObject *obj);

private:

	QPixmap genPix(PhotoData *photo, int32 size);
	void showAll();

	OverviewWidget *_overview;
	ScrollArea *_scroll;
	int32 _resizeIndex, _resizeSkip;

	PeerData *_peer;
	MediaOverviewType _type;
	History *_hist;
	QMenu *_menu;

	TextLinkPtr _contextMenuLnk;

	// photos
	int32 _photosInRow, _vsize;
	typedef struct {
		int32 vsize;
		bool medium;
		QPixmap pix;
	} CachedSize;
	typedef QMap<PhotoData*, CachedSize> CachedSizes;
	CachedSizes _cached;

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
	int32 _width, _height, _minHeight;

	QPoint _lastPos;
};

class OverviewWidget : public QWidget, public RPCSender, public Animated {
	Q_OBJECT

public:

	OverviewWidget(QWidget *parent, const PeerData *peer, MediaOverviewType type);

	void clear();

	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);

	void scrollBy(int32 add);

	void paintTopBar(QPainter &p, float64 over, int32 decreaseWidth);
	void topBarClick();

	PeerData *peer() const;
	MediaOverviewType type() const;
	void switchType(MediaOverviewType type);
	int32 lastWidth() const;
	int32 lastScrollTop() const;

	void animShow(const QPixmap &oldAnimCache, const QPixmap &bgAnimTopBarCache, bool back = false, int32 lastScrollTop = -1);
	bool animStep(float64 ms);

	void mediaOverviewUpdated(PeerData *peer);
	void msgUpdated(PeerId peer, HistoryItem *msg);

	~OverviewWidget();

public slots:

	void activate();
	void onScroll();

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

};

