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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "abstractbox.h"

class StickerSetInner : public TWidget, public RPCSender {
	Q_OBJECT

public:

	StickerSetInner(const MTPInputStickerSet &set);

	void init();

	void paintEvent(QPaintEvent *e);
	
	bool loaded() const;
	int32 notInstalled() const;
	bool official() const;
	QString title() const;
	QString shortName() const;

	void setScrollBottom(int32 bottom);
	void install();

	QString getTitle() const;

	~StickerSetInner();

signals:

	void updateButtons();
	void installed(uint64 id);

private:

	void gotSet(const MTPmessages_StickerSet &set);
	bool failedSet(const RPCError &error);

	void installDone(const MTPBool &result);
	bool installFailed(const RPCError &error);

	StickerPack _pack;
	bool _loaded;
	uint64 _setId, _setAccess;
	QString _title, _setTitle, _setShortName;
	int32 _setCount, _setHash, _setFlags;

	int32 _bottom;
	MTPInputStickerSet _input;

	mtpRequestId _installRequest;
};

class StickerSetBox : public ScrollableBox, public RPCSender {
	Q_OBJECT

public:

	StickerSetBox(const MTPInputStickerSet &set);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

public slots:

	void onStickersUpdated();
	void onAddStickers();
	void onShareStickers();
	void onUpdateButtons();

	void onScroll();

signals:

	void installed(uint64 id);

protected:

	void hideAll();
	void showAll();

private:

	StickerSetInner _inner;
	ScrollableBoxShadow _shadow;
	BoxButton _add, _share, _cancel, _done;
	QString _title;
};

class StickersInner : public TWidget {
	Q_OBJECT

public:

	StickersInner();

	void paintEvent(QPaintEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	
	void rebuild();
	bool savingStart() {
		if (_saving) return false;
		_saving = true;
		return true;
	}

	QVector<uint64> getOrder() const;
	QVector<uint64> getDisabledSets() const;

	void setVisibleScrollbar(int32 width);

	~StickersInner();

signals:

	void checkDraggingScroll(int localY);
	void noDraggingScroll();

public slots:

	void onUpdateSelected();

private:

	bool animStep_shifting(float64 ms);
	void paintRow(Painter &p, int32 index);
	void clear();
	void setRemoveSel(int32 removeSel);
	float64 aboveShadowOpacity() const;
	void updateAnimatedRegions();
	bool updateAnimatedValues();

	int32 _rowHeight;
	struct StickerSetRow {
		StickerSetRow(uint64 id, DocumentData *sticker, int32 count, const QString &title, bool official, bool disabled, int32 pixw, int32 pixh) : id(id)
			, sticker(sticker)
			, count(count)
			, title(title)
			, official(official)
			, disabled(disabled)
			, pixw(pixw)
			, pixh(pixh)
			, yadd(0, 0) {
		}
		uint64 id;
		DocumentData *sticker;
		int32 count;
		QString title;
		bool official, disabled;
		int32 pixw, pixh;
		anim::ivalue yadd;
	};
	typedef QList<StickerSetRow*> StickerSetRows;
	StickerSetRows _rows;
	QList<uint64> _animStartTimes;
	uint64 _aboveShadowFadeStart;
	anim::fvalue _aboveShadowFadeOpacity;
	Animation _a_shifting;

	int32 _itemsTop;

	bool _saving;

	int32 _removeSel, _removeDown, _removeWidth, _returnWidth, _restoreWidth;

	QPoint _mouse;
	int32 _selected;
	QPoint _dragStart;
	int32 _started, _dragging, _above;

	BoxShadow _aboveShadow;

	int32 _scrollbar;
};

class StickersBox : public ItemListBox, public RPCSender {
	Q_OBJECT

public:

	StickersBox();
	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);
	
	void closePressed();

public slots:

	void onStickersUpdated();

	void onCheckDraggingScroll(int localY);
	void onNoDraggingScroll();
	void onScrollTimer();

	void onSave();

protected:

	void hideAll();
	void showAll();

private:

	int32 countHeight() const;

	void disenableDone(const MTPBool &result, mtpRequestId req);
	bool disenableFail(const RPCError &error, mtpRequestId req);
	void reorderDone(const MTPBool &result);
	bool reorderFail(const RPCError &result);
	void saveOrder();

	StickersInner _inner;
	BoxButton _save, _cancel;
	QMap<mtpRequestId, NullType> _disenableRequests;
	mtpRequestId _reorderRequest;
	PlainShadow _topShadow;
	ScrollableBoxShadow _bottomShadow;

	QTimer _scrollTimer;
	int32 _scrollDelta;

	int32 _aboutWidth;
	Text _about;
	int32 _aboutHeight;

};

int32 stickerPacksCount(bool includeDisabledOfficial = false);
