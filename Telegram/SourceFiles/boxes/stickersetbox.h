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

#include "abstractbox.h"

class StickerSetInner : public TWidget, public RPCSender {
	Q_OBJECT

public:

	StickerSetInner(const MTPInputStickerSet &set);

	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);

	void paintEvent(QPaintEvent *e);

	bool loaded() const;
	int32 notInstalled() const;
	bool official() const;
	QString title() const;
	QString shortName() const;

	void setScrollBottom(int32 bottom);
	void install();

	~StickerSetInner();

public slots:

	void onPreview();

signals:

	void updateButtons();
	void installed(uint64 id);

private:

	int32 stickerFromGlobalPos(const QPoint &p) const;

	void gotSet(const MTPmessages_StickerSet &set);
	bool failedSet(const RPCError &error);

	void installDone(const MTPBool &result);
	bool installFail(const RPCError &error);

	StickerPack _pack;
	StickersByEmojiMap _emoji;
	bool _loaded = false;
	uint64 _setId = 0;
	uint64 _setAccess = 0;
	QString _title, _setTitle, _setShortName;
	int32 _setCount = 0;
	int32 _setHash = 0;
	MTPDstickerSet::Flags _setFlags = 0;

	int32 _bottom = 0;
	MTPInputStickerSet _input;

	mtpRequestId _installRequest = 0;

	QTimer _previewTimer;
	int32 _previewShown = -1;
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

private slots:
	void onInstalled(uint64 id);

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

namespace internal {
class StickersInner;
} // namespace internal

class StickersBox : public ItemListBox, public RPCSender {
	Q_OBJECT

public:

	enum class Section {
		Installed,
		Featured,
	};
	StickersBox(Section section = Section::Installed);
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

	Section _section;

	ChildWidget<internal::StickersInner> _inner;
	ChildWidget<BoxButton> _save = { nullptr };
	ChildWidget<BoxButton> _cancel = { nullptr };
	QMap<mtpRequestId, NullType> _disenableRequests;
	mtpRequestId _reorderRequest;
	PlainShadow _topShadow;
	ChildWidget<ScrollableBoxShadow> _bottomShadow = { nullptr };

	QTimer _scrollTimer;
	int32 _scrollDelta;

	int _aboutWidth = 0;
	Text _about;
	int _aboutHeight = 0;

};

int32 stickerPacksCount(bool includeDisabledOfficial = false);

namespace internal {

class StickersInner : public TWidget, public RPCSender {
	Q_OBJECT

public:
	using Section = StickersBox::Section;
	StickersInner(Section section);

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

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEvent(QEvent *e) override;

signals:
	void checkDraggingScroll(int localY);
	void noDraggingScroll();

public slots:
	void onUpdateSelected();

private:
	void paintFeaturedButton(Painter &p) const;

	void step_shifting(uint64 ms, bool timer);
	void paintRow(Painter &p, int32 index);
	void clear();
	void setActionSel(int32 actionSel);
	float64 aboveShadowOpacity() const;

	void installSet(uint64 setId);
	bool installFail(uint64 setId, const RPCError &error);
	void readFeaturedDone(const MTPBool &result);
	bool readFeaturedFail(const RPCError &error);

	Section _section;

	int32 _rowHeight;
	struct StickerSetRow {
		StickerSetRow(uint64 id, DocumentData *sticker, int32 count, const QString &title, bool installed, bool official, bool unread, bool disabled, int32 pixw, int32 pixh) : id(id)
			, sticker(sticker)
			, count(count)
			, title(title)
			, installed(installed)
			, official(official)
			, unread(unread)
			, disabled(disabled)
			, pixw(pixw)
			, pixh(pixh)
			, yadd(0, 0) {
		}
		uint64 id;
		DocumentData *sticker;
		int32 count;
		QString title;
		bool installed, official, unread, disabled;
		int32 pixw, pixh;
		anim::ivalue yadd;
	};
	typedef QList<StickerSetRow*> StickerSetRows;
	StickerSetRows _rows;
	QList<uint64> _animStartTimes;
	uint64 _aboveShadowFadeStart = 0;
	anim::fvalue _aboveShadowFadeOpacity = { 0., 0. };
	Animation _a_shifting;

	int32 _itemsTop;

	bool _saving = false;

	int _actionSel = -1;
	int _actionDown = -1;

	int _removeWidth, _returnWidth, _restoreWidth;

	QString _addText;
	int _addWidth;

	int _featuredHeight = 0;
	// Remember all the unread set ids to display unread dots.
	OrderedSet<uint64> _unreadSets;

	QPoint _mouse;
	int _selected = -2; // -1 - featured stickers button
	int _pressed = -2;
	QPoint _dragStart;
	int _started = -1;
	int _dragging = -1;
	int _above = -1;

	BoxShadow _aboveShadow;

	int32 _scrollbar = 0;
};

} // namespace internal
