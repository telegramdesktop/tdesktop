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

class ConfirmBox;

namespace Ui {
class PlainShadow;
} // namespace Ui

class StickersBox : public ItemListBox, public RPCSender {
	Q_OBJECT

public:
	enum class Section {
		Installed,
		Featured,
		Archived,
		ArchivedPart,
	};
	StickersBox(Section section = Section::Installed);
	StickersBox(const Stickers::Order &archivedIds);

	~StickersBox();

public slots:
	void onStickersUpdated();

	void onCheckDraggingScroll(int localY);
	void onNoDraggingScroll();
	void onScrollTimer();

	void onSave();

private slots:
	void onScroll();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void closePressed() override;
	void showAll() override;

private:
	void setup();
	int32 countHeight() const;
	void rebuildList();

	void disenableDone(const MTPmessages_StickerSetInstallResult &result, mtpRequestId req);
	bool disenableFail(const RPCError &error, mtpRequestId req);
	void reorderDone(const MTPBool &result);
	bool reorderFail(const RPCError &result);
	void saveOrder();

	void updateVisibleTopBottom();
	void checkLoadMoreArchived();
	void getArchivedDone(uint64 offsetId, const MTPmessages_ArchivedStickers &result);

	Section _section;

	class Inner;
	ChildWidget<Inner> _inner;
	ChildWidget<BoxButton> _save = { nullptr };
	ChildWidget<BoxButton> _cancel = { nullptr };
	OrderedSet<mtpRequestId> _disenableRequests;
	mtpRequestId _reorderRequest = 0;
	ChildWidget<Ui::PlainShadow> _topShadow = { nullptr };
	ChildWidget<ScrollableBoxShadow> _bottomShadow = { nullptr };

	QTimer _scrollTimer;
	int32 _scrollDelta = 0;

	int _aboutWidth = 0;
	Text _about;
	int _aboutHeight = 0;

	mtpRequestId _archivedRequestId = 0;
	bool _allArchivedLoaded = false;

};

int32 stickerPacksCount(bool includeDisabledOfficial = false);

// This class is hold in header because it requires Qt preprocessing.
class StickersBox::Inner : public TWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	using Section = StickersBox::Section;
	Inner(QWidget *parent, Section section);
	Inner(QWidget *parent, const Stickers::Order &archivedIds);

	void rebuild();
	void updateSize();
	void updateRows(); // refresh only pack cover stickers
	bool appendSet(const Stickers::Set &set);
	bool savingStart() {
		if (_saving) return false;
		_saving = true;
		return true;
	}

	Stickers::Order getOrder() const;
	Stickers::Order getDisabledSets() const;

	void setVisibleScrollbar(int32 width);
	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	~Inner();

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
	void onClearRecent();
	void onClearBoxDestroyed(QObject *box);

private slots:
	void onImageLoaded();

private:
	void setup();
	void paintButton(Painter &p, int y, bool selected, const QString &text, int badgeCounter) const;

	void step_shifting(uint64 ms, bool timer);
	void paintRow(Painter &p, int32 index);
	void clear();
	void setActionSel(int32 actionSel);
	float64 aboveShadowOpacity() const;

	void readVisibleSets();

	void installSet(uint64 setId);
	void installDone(const MTPmessages_StickerSetInstallResult &result);
	bool installFail(uint64 setId, const RPCError &error);

	Section _section;
	Stickers::Order _archivedIds;

	int32 _rowHeight;
	struct StickerSetRow {
		StickerSetRow(uint64 id, DocumentData *sticker, int32 count, const QString &title, int titleWidth, bool installed, bool official, bool unread, bool disabled, bool recent, int32 pixw, int32 pixh) : id(id)
			, sticker(sticker)
			, count(count)
			, title(title)
			, titleWidth(titleWidth)
			, installed(installed)
			, official(official)
			, unread(unread)
			, disabled(disabled)
			, recent(recent)
			, pixw(pixw)
			, pixh(pixh)
			, yadd(0, 0) {
		}
		uint64 id;
		DocumentData *sticker;
		int32 count;
		QString title;
		int titleWidth;
		bool installed, official, unread, disabled, recent;
		int32 pixw, pixh;
		anim::ivalue yadd;
	};
	using StickerSetRows = QList<StickerSetRow*>;

	void rebuildAppendSet(const Stickers::Set &set, int maxNameWidth);
	void fillSetCover(const Stickers::Set &set, DocumentData **outSticker, int *outWidth, int *outHeight) const;
	int fillSetCount(const Stickers::Set &set) const;
	QString fillSetTitle(const Stickers::Set &set, int maxNameWidth, int *outTitleWidth) const;
	void fillSetFlags(const Stickers::Set &set, bool *outRecent, bool *outInstalled, bool *outOfficial, bool *outUnread, bool *outDisabled);

	int countMaxNameWidth() const;

	StickerSetRows _rows;
	QList<uint64> _animStartTimes;
	uint64 _aboveShadowFadeStart = 0;
	anim::fvalue _aboveShadowFadeOpacity = { 0., 0. };
	Animation _a_shifting;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _itemsTop = 0;

	bool _saving = false;

	int _actionSel = -1;
	int _actionDown = -1;

	int _clearWidth, _removeWidth, _returnWidth, _restoreWidth;

	ConfirmBox *_clearBox = nullptr;

	int _buttonHeight = 0;
	bool _hasFeaturedButton = false;
	bool _hasArchivedButton = false;

	QPoint _mouse;
	int _selected = -3; // -2 - featured stickers button, -1 - archived stickers button
	int _pressed = -2;
	QPoint _dragStart;
	int _started = -1;
	int _dragging = -1;
	int _above = -1;

	Ui::RectShadow _aboveShadow;

	int32 _scrollbar = 0;
};
