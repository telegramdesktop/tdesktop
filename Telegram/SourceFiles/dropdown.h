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

#include "gui/twidget.h"
#include "gui/boxshadow.h"

class Dropdown : public TWidget, public Animated {
	Q_OBJECT

public:

	Dropdown(QWidget *parent, const style::dropdown &st = st::dropdownDef);

	IconedButton *addButton(IconedButton *button);
	void resetButtons();
	void updateButtons();

	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);

	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void otherEnter();
	void otherLeave();

	void fastHide();
	void ignoreShow(bool ignore = true);

	bool animStep(float64 ms);

	bool eventFilter(QObject *obj, QEvent *e);

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || animating()) return false;

		return QRect(_st.padding.left(),
					 _st.padding.top(),
					 _width - _st.padding.left() - _st.padding.right(),
					 _height - _st.padding.top() - _st.padding.bottom()
					 ).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

signals:

	void hiding();

public slots:

	void hideStart();
	void hideFinish();

	void showStart();
	void onWndActiveChanged();

	void buttonStateChanged(int oldState, ButtonStateChangeSource source);

private:

	void adjustButtons();

	bool _ignore;

	typedef QVector<IconedButton*> Buttons;
	Buttons _buttons;

	int32 _selected;

	const style::dropdown &_st;

	int32 _width, _height;
	bool _hiding;

	anim::fvalue a_opacity;

	QTimer _hideTimer;

	BoxShadow _shadow;

};

class DragArea : public TWidget, public Animated {
	Q_OBJECT

public:

	DragArea(QWidget *parent);

	void paintEvent(QPaintEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void dragEnterEvent(QDragEnterEvent *e);
	void dragLeaveEvent(QDragLeaveEvent *e);
	void dropEvent(QDropEvent *e);
	void dragMoveEvent(QDragMoveEvent *e);

	void setText(const QString &text, const QString &subtext);

	void otherEnter();
	void otherLeave();

	void fastHide();

	bool animStep(float64 ms);

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || animating()) return false;

		return QRect(st::dragPadding.left(),
					 st::dragPadding.top(),
					 width() - st::dragPadding.left() - st::dragPadding.right(),
					 height() - st::dragPadding.top() - st::dragPadding.bottom()
					 ).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

signals:

	void dropped(const QMimeData *data);

public slots:

	void hideStart();
	void hideFinish();

	void showStart();

private:

	bool _hiding, _in;

	anim::fvalue a_opacity;
	anim::cvalue a_color;

	BoxShadow _shadow;

	QString _text, _subtext;

};

class EmojiPanel;
static const int EmojiColorsCount = 5;

class EmojiColorPicker : public TWidget, public Animated {
	Q_OBJECT

public:

	EmojiColorPicker();

	void showEmoji(uint32 code);

	void paintEvent(QPaintEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);

	bool animStep(float64 ms);
	void showStart();

	void clearSelection(bool fast = false);

public slots:

	void hideStart(bool fast = false);

signals:

	void emojiSelected(EmojiPtr emoji);
	void hidden();

private:

	void drawVariant(Painter &p, int variant);

	void updateSelected();

	bool _ignoreShow;

	EmojiPtr _variants[EmojiColorsCount + 1];

	typedef QMap<int32, uint64> EmojiAnimations; // index - showing, -index - hiding
	EmojiAnimations _emojiAnimations;

	float64 _hovers[EmojiColorsCount + 1];

	int32 _selected, _pressedSel;
	QPoint _lastMousePos;

	bool _hiding;
	QPixmap _cache;

	anim::fvalue a_opacity;

	QTimer _hideTimer;

	BoxShadow _shadow;

};

class EmojiPanInner : public TWidget, public Animated {
	Q_OBJECT

public:

	EmojiPanInner();

	void setMaxHeight(int32 h);
	void paintEvent(QPaintEvent *e);

	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);
	void leaveToChildEvent(QEvent *e);
	void enterFromChildEvent(QEvent *e);

	bool animStep(float64 ms);
	void hideFinish();

	void showEmojiPack(DBIEmojiTab packIndex);

	void clearSelection(bool fast = false);

	DBIEmojiTab currentTab(int yOffset) const;

	void refreshRecent();

	void setScrollTop(int top);

	void fillPanels(QVector<EmojiPanel*> &panels);
	void refreshPanels(QVector<EmojiPanel*> &panels);
	
public slots:

	void updateSelected();
	void onSaveConfig();

	void onShowPicker();
	void onPickerHidden();
	void onColorSelected(EmojiPtr emoji);

	bool checkPickerHide();

signals:

	void selected(EmojiPtr emoji);

	void switchToStickers();

	void scrollToY(int y);
	void disableScroll(bool dis);

	void needRefreshPanels();

private:

	int32 _maxHeight;

	int32 countHeight();
	void selectEmoji(EmojiPtr emoji);

	QRect emojiRect(int tab, int sel);

	typedef QMap<int32, uint64> Animations; // index - showing, -index - hiding
	Animations _animations;

	int32 _top, _counts[emojiTabCount];

	QVector<EmojiPtr> _emojis[emojiTabCount];
	QVector<float64> _hovers[emojiTabCount];

	int32 _esize;

	int32 _selected, _pressedSel, _pickerSel;
	QPoint _lastMousePos;

	QTimer _saveConfigTimer;

	EmojiColorPicker _picker;
	QTimer _showPickerTimer;
};

struct StickerIcon {
	StickerIcon() : setId(RecentStickerSetId), sticker(0), pixw(0), pixh(0) {
	}
	StickerIcon(uint64 setId, DocumentData *sticker, int32 pixw, int32 pixh) : setId(setId), sticker(sticker), pixw(pixw), pixh(pixh) {
	}
	uint64 setId;
	DocumentData *sticker;
	int32 pixw, pixh;
};

class StickerPanInner : public TWidget, public Animated {
	Q_OBJECT

public:

	StickerPanInner();

	void setMaxHeight(int32 h);
	void paintEvent(QPaintEvent *e);

	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);
	void leaveToChildEvent(QEvent *e);
	void enterFromChildEvent(QEvent *e);

	bool animStep(float64 ms);

	void showStickerSet(uint64 setId);

	void clearSelection(bool fast = false);

	void refreshStickers();
	void refreshRecent(bool resize = true);

	void fillIcons(QVector<StickerIcon> &icons);
	void fillPanels(QVector<EmojiPanel*> &panels);
	void refreshPanels(QVector<EmojiPanel*> &panels);

	void setScrollTop(int top);
	void preloadImages();

	uint64 currentSet(int yOffset) const;

public slots:

	void updateSelected();
	void onSettings();
	void onPreview();

signals:

	void selected(DocumentData *sticker);
	void removing(quint64 setId);

	void refreshIcons();

	void switchToEmoji();

	void scrollToY(int y);
	void disableScroll(bool dis);
	void needRefreshPanels();

private:

	int32 _maxHeight;

	void appendSet(uint64 setId);

	int32 countHeight();
	void selectEmoji(EmojiPtr emoji);
	QRect stickerRect(int tab, int sel);

	typedef QMap<int32, uint64> Animations; // index - showing, -index - hiding
	Animations _animations;

	int32 _top;

	struct DisplayedSet {
		DisplayedSet(uint64 id, int32 flags, const QString &title, int32 hoversSize, const StickerPack &pack = StickerPack()) : id(id), flags(flags), title(title), hovers(hoversSize, 0), pack(pack) {
		}
		uint64 id;
		int32 flags;
		QString title;
		QVector<float64> hovers;
		StickerPack pack;
	};
	QList<DisplayedSet> _sets;
	QList<bool> _custom;

	int32 _selected, _pressedSel;
	QPoint _lastMousePos;

	LinkButton _settings;

	QTimer _previewTimer;
	bool _previewShown;
};

class EmojiPanel : public TWidget {
	Q_OBJECT

public:

	EmojiPanel(QWidget *parent, const QString &text, uint64 setId, bool special, int32 wantedY); // NoneStickerSetId if in emoji
	void setText(const QString &text);
	void setDeleteVisible(bool isVisible);

	void paintEvent(QPaintEvent *e);
	void mousePressEvent(QMouseEvent *e);

	int32 wantedY() const {
		return _wantedY;
	}
	void setWantedY(int32 y) {
		_wantedY = y;
	}

signals:

	void deleteClicked(quint64 setId);
	void mousePressed();

public slots:

	void onDelete();

private:

	void updateText();

	int32 _wantedY;
	QString _text, _fullText;
	uint64 _setId;
	bool _special, _deleteVisible;
	IconedButton *_delete;

};

class EmojiSwitchButton : public Button {
public:

	EmojiSwitchButton(QWidget *parent, bool toStickers); // otherwise toEmoji
	void paintEvent(QPaintEvent *e);

protected:

	bool _toStickers;
	QString _text;
	int32 _textWidth;

};

class EmojiPan : public TWidget, public Animated {
	Q_OBJECT

public:

	EmojiPan(QWidget *parent);

	void setMaxHeight(int32 h);
	void paintEvent(QPaintEvent *e);

	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void otherEnter();
	void otherLeave();

	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);

	bool event(QEvent *e);

	void fastHide();
	bool hiding() const {
		return _hiding || _hideTimer.isActive();
	}

	bool animStep(float64 ms);

	bool iconAnim(float64 ms);

	bool eventFilter(QObject *obj, QEvent *e);
	void stickersInstalled(uint64 setId);

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || !_cache.isNull()) return false;

		return QRect(st::dropdownDef.padding.left(),
					 st::dropdownDef.padding.top(),
					 _width - st::dropdownDef.padding.left() - st::dropdownDef.padding.right(),
					 _height - st::dropdownDef.padding.top() - st::dropdownDef.padding.bottom()
					 ).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

public slots:

	void refreshStickers();

	void hideStart();
	void hideFinish();

	void showStart();
	void onWndActiveChanged();

	void onTabChange();
	void onScroll();
	void onSwitch();

	void onRemoveSet(quint64 setId);
	void onRemoveSetSure();
	void onDelayedHide();

	void onRefreshIcons();
	void onRefreshPanels();

signals:

	void emojiSelected(EmojiPtr emoji);
	void stickerSelected(DocumentData *sticker);
	void updateStickers();

private:

	int32 _maxHeight;
	bool _horizontal;

	void leaveToChildEvent(QEvent *e);

	void updateSelected();
	void updateIcons();

	void prepareTab(int32 &left, int32 top, int32 _width, FlatRadiobutton &tab);
	void updatePanelsPositions(const QVector<EmojiPanel*> &panels, int32 st);

	void showAll();
	void hideAll();

	bool _noTabUpdate;

	int32 _width, _height;
	bool _hiding;
	QPixmap _cache;

	anim::fvalue a_opacity;

	QTimer _hideTimer;

	BoxShadow _shadow;

	FlatRadiobutton _recent, _people, _nature, _food, _activity, _travel, _objects, _symbols;
	QVector<StickerIcon> _icons;
	QVector<float64> _iconHovers;
	int32 _iconOver, _iconSel, _iconDown;
	bool _iconsDragging;
	typedef QMap<int32, uint64> Animations; // index - showing, -index - hiding
	Animations _iconAnimations;
	Animation _iconAnim;
	QPoint _iconsMousePos, _iconsMouseDown;
	int32 _iconsLeft, _iconsTop;
	int32 _iconsStartX, _iconsMax;
	anim::ivalue _iconsX, _iconSelX;
	uint64 _iconsStartAnim;

	bool _stickersShown;
	QPixmap _fromCache, _toCache;
	anim::ivalue a_fromCoord, a_toCoord;
	anim::fvalue a_fromAlpha, a_toAlpha;
	uint64 _moveStart;

	ScrollArea e_scroll;
	EmojiPanInner e_inner;
	QVector<EmojiPanel*> e_panels;
	EmojiSwitchButton e_switch;
	ScrollArea s_scroll;
	StickerPanInner s_inner;
	QVector<EmojiPanel*> s_panels;
	EmojiSwitchButton s_switch;

	uint64 _removingSetId;

};

typedef QList<UserData*> MentionRows;
typedef QList<QString> HashtagRows;
typedef QList<QPair<UserData*, const BotCommand*> > BotCommandRows;

class MentionsDropdown;
class MentionsInner : public QWidget {
	Q_OBJECT

public:

	MentionsInner(MentionsDropdown *parent, MentionRows *rows, HashtagRows *hrows, BotCommandRows *crows);

	void paintEvent(QPaintEvent *e);

	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);

	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);

	void clearSel();
	bool moveSel(int direction);
	bool select();

	QString getSelected() const;

signals:

	void chosen(QString mentionOrHashtag);
	void mustScrollTo(int scrollToTop, int scrollToBottom);

public slots:

	void onParentGeometryChanged();
	void onUpdateSelected(bool force = false);

private:

	void setSel(int sel, bool scroll = false);

	MentionsDropdown *_parent;
	MentionRows *_rows;
	HashtagRows *_hrows;
	BotCommandRows *_crows;
	int32 _sel;
	bool _mouseSel;
	QPoint _mousePos;

	bool _overDelete;
};

class MentionsDropdown : public TWidget, public Animated {
	Q_OBJECT

public:

	MentionsDropdown(QWidget *parent);

	void paintEvent(QPaintEvent *e);

	void fastHide();

	bool clearFilteredCommands();
	void showFiltered(PeerData *peer, QString start);
	void updateFiltered(bool toDown = false);
	void setBoundings(QRect boundings);

	bool animStep(float64 ms);

	const QString &filter() const;
	ChatData *chat() const;
	ChannelData *channel() const;
	UserData *user() const;

	int32 innerTop();
	int32 innerBottom();

	bool eventFilter(QObject *obj, QEvent *e);
	QString getSelected() const;

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || !testAttribute(Qt::WA_OpaquePaintEvent)) return false;

		return rect().contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

	~MentionsDropdown();

signals:

	void chosen(QString mentionOrHashtag);

public slots:

	void hideStart();
	void hideFinish();

	void showStart();

private:

	void recount(bool toDown = false);

	QPixmap _cache;
	MentionRows _rows;
	HashtagRows _hrows;
	BotCommandRows _crows;

	ScrollArea _scroll;
	MentionsInner _inner;

	ChatData *_chat;
	UserData *_user;
	ChannelData *_channel;
	QString _filter;
	QRect _boundings;

	int32 _width, _height;
	bool _hiding;

	anim::fvalue a_opacity;

	QTimer _hideTimer;

	BoxShadow _shadow;

};
