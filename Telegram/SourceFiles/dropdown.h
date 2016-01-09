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

class Dropdown : public TWidget {
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

	void step_appearance(float64 ms, bool timer);

	bool eventFilter(QObject *obj, QEvent *e);

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || _a_appearance.animating()) return false;

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
	Animation _a_appearance;

	QTimer _hideTimer;

	BoxShadow _shadow;

};

class DragArea : public TWidget {
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

	void step_appearance(float64 ms, bool timer);

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || _a_appearance.animating()) return false;

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
	Animation _a_appearance;

	BoxShadow _shadow;

	QString _text, _subtext;

};

class EmojiPanel;
static const int EmojiColorsCount = 5;

class EmojiColorPicker : public TWidget {
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

	void step_appearance(float64 ms, bool timer);
	void step_selected(uint64 ms, bool timer);
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
	Animation _a_selected;

	float64 _hovers[EmojiColorsCount + 1];

	int32 _selected, _pressedSel;
	QPoint _lastMousePos;

	bool _hiding;
	QPixmap _cache;

	anim::fvalue a_opacity;
	Animation _a_appearance;

	QTimer _hideTimer;

	BoxShadow _shadow;

};

class EmojiPanInner : public TWidget {
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

	void step_selected(uint64 ms, bool timer);
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
	void saveConfigDelayed(int32 delay);

private:

	int32 _maxHeight;

	int32 countHeight();
	void selectEmoji(EmojiPtr emoji);

	QRect emojiRect(int tab, int sel);

	typedef QMap<int32, uint64> Animations; // index - showing, -index - hiding
	Animations _animations;
	Animation _a_selected;

	int32 _top, _counts[emojiTabCount];

	QVector<EmojiPtr> _emojis[emojiTabCount];
	QVector<float64> _hovers[emojiTabCount];

	int32 _esize;

	int32 _selected, _pressedSel, _pickerSel;
	QPoint _lastMousePos;

	EmojiColorPicker _picker;
	QTimer _showPickerTimer;
};

struct StickerIcon {
	StickerIcon(uint64 setId) : setId(setId), sticker(0), pixw(0), pixh(0) {
	}
	StickerIcon(uint64 setId, DocumentData *sticker, int32 pixw, int32 pixh) : setId(setId), sticker(sticker), pixw(pixw), pixh(pixh) {
	}
	uint64 setId;
	DocumentData *sticker;
	int32 pixw, pixh;
};

class StickerPanInner : public TWidget {
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

	void step_selected(uint64 ms, bool timer);

	void hideFinish(bool completely);
	void showFinish();
	void showStickerSet(uint64 setId);
	void updateShowingSavedGifs();

	bool showSectionIcons() const;
	void clearSelection(bool fast = false);

	void refreshStickers();
	void refreshRecentStickers(bool resize = true);
	void refreshSavedGifs();
	int32 refreshInlineRows(UserData *bot, const InlineResults &results, bool resultsDeleted);
	void refreshRecent();
	void inlineBotChanged();
	void hideInlineRowsPanel();
	void clearInlineRowsPanel();

	void fillIcons(QList<StickerIcon> &icons);
	void fillPanels(QVector<EmojiPanel*> &panels);
	void refreshPanels(QVector<EmojiPanel*> &panels);

	void setScrollTop(int top);
	void preloadImages();

	uint64 currentSet(int yOffset) const;

	void ui_repaintInlineItem(const LayoutInlineItem *layout);
	bool ui_isInlineItemVisible(const LayoutInlineItem *layout);
	bool ui_isInlineItemBeingChosen();

	bool inlineResultsShown() const {
		return _showingInlineItems && !_showingSavedGifs;
	}
	int32 countHeight(bool plain = false);

	~StickerPanInner() {
		clearInlineRows(true);
		deleteUnusedGifLayouts();
		deleteUnusedInlineLayouts();
	}

public slots:

	void updateSelected();
	void onSettings();
	void onPreview();
	void onUpdateInlineItems();

signals:

	void selected(DocumentData *sticker);
	void selected(PhotoData *photo);
	void selected(InlineResult *result, UserData *bot);

	void removing(quint64 setId);

	void refreshIcons();
	void emptyInlineRows();

	void switchToEmoji();

	void scrollToY(int y);
	void scrollUpdated();
	void disableScroll(bool dis);
	void needRefreshPanels();

	void saveConfigDelayed(int32 delay);

private:

	void paintInlineItems(Painter &p, const QRect &r);
	void paintStickers(Painter &p, const QRect &r);

	int32 _maxHeight;

	void appendSet(uint64 setId);

	void selectEmoji(EmojiPtr emoji);
	QRect stickerRect(int tab, int sel);

	typedef QMap<int32, uint64> Animations; // index - showing, -index - hiding
	Animations _animations;
	Animation _a_selected;

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

	bool _showingSavedGifs, _showingInlineItems;
	bool _setGifCommand;
	UserData *_inlineBot;
	QString _inlineBotTitle;
	uint64 _lastScrolled;
	QTimer _updateInlineItems;
	bool _inlineWithThumb;

	typedef QVector<LayoutInlineItem*> InlineItems;
	struct InlineRow {
		InlineRow() : height(0) {
		}
		int32 height;
		InlineItems items;
	};
	typedef QVector<InlineRow> InlineRows;
	InlineRows _inlineRows;
	void clearInlineRows(bool resultsDeleted);

	typedef QMap<DocumentData*, LayoutInlineGif*> GifLayouts;
	GifLayouts _gifLayouts;
	LayoutInlineGif *layoutPrepareSavedGif(DocumentData *doc, int32 position);

	typedef QMap<InlineResult*, LayoutInlineItem*> InlineLayouts;
	InlineLayouts _inlineLayouts;
	LayoutInlineItem *layoutPrepareInlineResult(InlineResult *result, int32 position);

	bool inlineRowsAddItem(DocumentData *savedGif, InlineResult *result, InlineRow &row, int32 &sumWidth);
	bool inlineRowFinalize(InlineRow &row, int32 &sumWidth, bool force = false);

	InlineRow &layoutInlineRow(InlineRow &row, int32 sumWidth = 0);
	void deleteUnusedGifLayouts();

	void deleteUnusedInlineLayouts();

	int32 validateExistingInlineRows(const InlineResults &results);
	int32 _selected, _pressedSel;
	QPoint _lastMousePos;
	TextLinkPtr _linkOver, _linkDown;

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
	void updateText(const QString &inlineBotUsername = QString());

protected:

	bool _toStickers;
	QString _text;
	int32 _textWidth;

};

class EmojiPan : public TWidget, public RPCSender {
	Q_OBJECT

public:

	EmojiPan(QWidget *parent);

	void setMaxHeight(int32 h);
	void paintEvent(QPaintEvent *e);

	void moveBottom(int32 bottom, bool force = false);

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

	void step_appearance(float64 ms, bool timer);
	void step_slide(float64 ms, bool timer);
	void step_icons(uint64 ms, bool timer);

	bool eventFilter(QObject *obj, QEvent *e);
	void stickersInstalled(uint64 setId);

	void queryInlineBot(UserData *bot, QString query);
	void clearInlineBot();

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || !_cache.isNull()) return false;

		return QRect(st::dropdownDef.padding.left(),
					 st::dropdownDef.padding.top(),
					 _width - st::dropdownDef.padding.left() - st::dropdownDef.padding.right(),
					 _height - st::dropdownDef.padding.top() - st::dropdownDef.padding.bottom()
					 ).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

	void ui_repaintInlineItem(const LayoutInlineItem *layout);
	bool ui_isInlineItemVisible(const LayoutInlineItem *layout);
	bool ui_isInlineItemBeingChosen();

	bool inlineResultsShown() const {
		return s_inner.inlineResultsShown();
	}
	void notify_automaticLoadSettingsChangedGif();

public slots:

	void refreshStickers();
	void refreshSavedGifs();

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

	void onSaveConfig();
	void onSaveConfigDelayed(int32 delay);

	void onInlineRequest();
	void onEmptyInlineRows();

signals:

	void emojiSelected(EmojiPtr emoji);
	void stickerSelected(DocumentData *sticker);
	void photoSelected(PhotoData *photo);
	void inlineResultSelected(InlineResult *result, UserData *bot);

	void updateStickers();

private:

	void validateSelectedIcon(bool animated = false);

	int32 _maxHeight, _contentMaxHeight, _contentHeight, _contentHeightEmoji, _contentHeightStickers;
	bool _horizontal;
	void updateContentHeight();

	void leaveToChildEvent(QEvent *e);
	void hideAnimated();
	void prepareShowHideCache();

	void updateSelected();
	void updateIcons();

	void prepareTab(int32 &left, int32 top, int32 _width, FlatRadiobutton &tab);
	void updatePanelsPositions(const QVector<EmojiPanel*> &panels, int32 st);

	void showAll();
	void hideAll();

	bool _noTabUpdate;

	int32 _width, _height, _bottom;
	bool _hiding;
	QPixmap _cache;

	anim::fvalue a_opacity;
	Animation _a_appearance;

	QTimer _hideTimer;

	BoxShadow _shadow;

	FlatRadiobutton _recent, _people, _nature, _food, _activity, _travel, _objects, _symbols;
	QList<StickerIcon> _icons;
	QVector<float64> _iconHovers;
	int32 _iconOver, _iconSel, _iconDown;
	bool _iconsDragging;
	typedef QMap<int32, uint64> Animations; // index - showing, -index - hiding
	Animations _iconAnimations;
	Animation _a_icons;
	QPoint _iconsMousePos, _iconsMouseDown;
	int32 _iconsLeft, _iconsTop;
	int32 _iconsStartX, _iconsMax;
	anim::ivalue _iconsX, _iconSelX;
	uint64 _iconsStartAnim;

	bool _stickersShown, _shownFromInlineQuery;
	QPixmap _fromCache, _toCache;
	anim::ivalue a_fromCoord, a_toCoord;
	anim::fvalue a_fromAlpha, a_toAlpha;
	Animation _a_slide;

	ScrollArea e_scroll;
	EmojiPanInner e_inner;
	QVector<EmojiPanel*> e_panels;
	EmojiSwitchButton e_switch;
	ScrollArea s_scroll;
	StickerPanInner s_inner;
	QVector<EmojiPanel*> s_panels;
	EmojiSwitchButton s_switch;

	uint64 _removingSetId;

	QTimer _saveConfigTimer;

	// inline bots
	struct InlineCacheEntry {
		~InlineCacheEntry() {
			clearResults();
		}
		QString nextOffset;
		InlineResults results;
		void clearResults() {
			for (int32 i = 0, l = results.size(); i < l; ++i) {
				delete results.at(i);
			}
			results.clear();
		}
	};
	typedef QMap<QString, InlineCacheEntry*> InlineCache;
	InlineCache _inlineCache;
	QTimer _inlineRequestTimer;

	void inlineBotChanged();
	int32 showInlineRows(bool newResults);
	bool hideOnNoInlineResults();
	void recountContentMaxHeight();
	bool refreshInlineRows(int32 *added = 0);
	UserData *_inlineBot;
	QString _inlineQuery, _inlineNextQuery, _inlineNextOffset;
	mtpRequestId _inlineRequestId;
	void inlineResultsDone(const MTPmessages_BotResults &result);
	bool inlineResultsFail(const RPCError &error);

};

typedef QList<UserData*> MentionRows;
typedef QList<QString> HashtagRows;
typedef QList<QPair<UserData*, const BotCommand*> > BotCommandRows;

class MentionsDropdown;
class MentionsInner : public TWidget {
	Q_OBJECT

public:

	MentionsInner(MentionsDropdown *parent, MentionRows *mrows, HashtagRows *hrows, BotCommandRows *brows, StickerPack *srows);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);

	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);

	void clearSel();
	bool moveSel(int key);
	bool select();

	void setRecentInlineBotsInRows(int32 bots);

	QString getSelected() const;

signals:

	void chosen(QString mentionOrHashtag);
	void selected(DocumentData *sticker);
	void mustScrollTo(int scrollToTop, int scrollToBottom);

public slots:

	void onParentGeometryChanged();
	void onUpdateSelected(bool force = false);

private:

	void updateSelectedRow();
	void setSel(int sel, bool scroll = false);

	MentionsDropdown *_parent;
	MentionRows *_mrows;
	HashtagRows *_hrows;
	BotCommandRows *_brows;
	StickerPack *_srows;
	int32 _stickersPerRow, _recentInlineBotsInRows;
	int32 _sel;
	bool _mouseSel;
	QPoint _mousePos;

	bool _overDelete;
};

class MentionsDropdown : public TWidget {
	Q_OBJECT

public:

	MentionsDropdown(QWidget *parent);

	void paintEvent(QPaintEvent *e);

	void fastHide();

	bool clearFilteredBotCommands();
	void showFiltered(PeerData *peer, QString query, bool start);
	void showStickers(EmojiPtr emoji);
	void updateFiltered(bool resetScroll = false);
	void setBoundings(QRect boundings);

	void step_appearance(float64 ms, bool timer);

	const QString &filter() const;
	ChatData *chat() const;
	ChannelData *channel() const;
	UserData *user() const;

	int32 innerTop();
	int32 innerBottom();

	bool eventFilter(QObject *obj, QEvent *e);
	QString getSelected() const;

	bool stickersShown() const {
		return !_srows.isEmpty();
	}

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || !testAttribute(Qt::WA_OpaquePaintEvent)) return false;

		return rect().contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

	~MentionsDropdown();

signals:

	void chosen(QString mentionOrHashtag);
	void stickerSelected(DocumentData *sticker);

public slots:

	void hideStart();
	void hideFinish();

	void showStart();

private:

	void recount(bool resetScroll = false);

	QPixmap _cache;
	MentionRows _mrows;
	HashtagRows _hrows;
	BotCommandRows _brows;
	StickerPack _srows;

	void rowsUpdated(const MentionRows &mrows, const HashtagRows &hrows, const BotCommandRows &brows, const StickerPack &srows, bool resetScroll);

	ScrollArea _scroll;
	MentionsInner _inner;

	ChatData *_chat;
	UserData *_user;
	ChannelData *_channel;
	EmojiPtr _emoji;
	QString _filter;
	QRect _boundings;
	bool _addInlineBots;

	int32 _width, _height;
	bool _hiding;

	anim::fvalue a_opacity;
	Animation _a_appearance;

	QTimer _hideTimer;

	BoxShadow _shadow;

};
