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

#include "ui/twidget.h"
#include "ui/effects/rect_shadow.h"

namespace InlineBots {
namespace Layout {
class ItemBase;
} // namespace Layout
class Result;
} // namespace InlineBots

namespace internal {

constexpr int InlineItemsMaxPerRow = 5;
constexpr int EmojiColorsCount = 5;

using InlineResult = InlineBots::Result;
using InlineResults = QList<InlineBots::Result*>;
using InlineItem = InlineBots::Layout::ItemBase;

struct InlineCacheEntry {
	~InlineCacheEntry() {
		clearResults();
	}
	QString nextOffset;
	QString switchPmText, switchPmStartToken;
	InlineResults results; // owns this results list
	void clearResults();
};

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

	void clearSelection(bool fast = false);

	void hideFast();

public slots:
	void showAnimated();
	void hideAnimated();

signals:
	void emojiSelected(EmojiPtr emoji);
	void hidden();

private:
	void drawVariant(Painter &p, int variant);

	void updateSelected();

	bool _ignoreShow = false;

	EmojiPtr _variants[EmojiColorsCount + 1];

	typedef QMap<int32, uint64> EmojiAnimations; // index - showing, -index - hiding
	EmojiAnimations _emojiAnimations;
	Animation _a_selected;

	float64 _hovers[EmojiColorsCount + 1];

	int _selected = -1;
	int _pressedSel = -1;
	QPoint _lastMousePos;

	bool _hiding = false;
	QPixmap _cache;

	anim::fvalue a_opacity;
	Animation _a_appearance;

	QTimer _hideTimer;

	Ui::RectShadow _shadow;

};

class EmojiPanel;
class EmojiPanInner : public TWidget {
	Q_OBJECT

public:
	EmojiPanInner();

	void setMaxHeight(int32 h);
	void paintEvent(QPaintEvent *e) override;

	void step_selected(uint64 ms, bool timer);
	void hideFinish();

	void showEmojiPack(DBIEmojiTab packIndex);

	void clearSelection(bool fast = false);

	DBIEmojiTab currentTab(int yOffset) const;

	void refreshRecent();

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	void fillPanels(QVector<EmojiPanel*> &panels);
	void refreshPanels(QVector<EmojiPanel*> &panels);

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;

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

	int countHeight();
	void selectEmoji(EmojiPtr emoji);

	QRect emojiRect(int tab, int sel);

	typedef QMap<int32, uint64> Animations; // index - showing, -index - hiding
	Animations _animations;
	Animation _a_selected;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _counts[emojiTabCount];

	QVector<EmojiPtr> _emojis[emojiTabCount];
	QVector<float64> _hovers[emojiTabCount];

	int32 _esize;

	int _selected = -1;
	int _pressedSel = -1;
	int _pickerSel = -1;
	QPoint _lastMousePos;

	EmojiColorPicker _picker;
	QTimer _showPickerTimer;
};

struct StickerIcon {
	StickerIcon(uint64 setId) : setId(setId) {
	}
	StickerIcon(uint64 setId, DocumentData *sticker, int32 pixw, int32 pixh) : setId(setId), sticker(sticker), pixw(pixw), pixh(pixh) {
	}
	uint64 setId;
	DocumentData *sticker = nullptr;
	int pixw = 0;
	int pixh = 0;
};

class StickerPanInner : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	StickerPanInner();

	void setMaxHeight(int32 h);
	void paintEvent(QPaintEvent *e) override;

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
	int refreshInlineRows(UserData *bot, const InlineCacheEntry *results, bool resultsDeleted);
	void refreshRecent();
	void inlineBotChanged();
	void hideInlineRowsPanel();
	void clearInlineRowsPanel();

	void fillIcons(QList<StickerIcon> &icons);
	void fillPanels(QVector<EmojiPanel*> &panels);
	void refreshPanels(QVector<EmojiPanel*> &panels);

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;
	void preloadImages();

	uint64 currentSet(int yOffset) const;

	void notify_inlineItemLayoutChanged(const InlineItem *layout);
	void ui_repaintInlineItem(const InlineItem *layout);
	bool ui_isInlineItemVisible(const InlineItem *layout);
	bool ui_isInlineItemBeingChosen();

	bool inlineResultsShown() const {
		return (_section == Section::Inlines);
	}
	int countHeight(bool plain = false);

	void installedLocally(uint64 setId);
	void notInstalledLocally(uint64 setId);
	void clearInstalledLocally();

	~StickerPanInner();

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;

private slots:
	void updateSelected();
	void onSettings();
	void onPreview();
	void onUpdateInlineItems();
	void onSwitchPm();

signals:
	void selected(DocumentData *sticker);
	void selected(PhotoData *photo);
	void selected(InlineBots::Result *result, UserData *bot);

	void displaySet(quint64 setId);
	void installSet(quint64 setId);
	void removeSet(quint64 setId);

	void refreshIcons(bool scrollAnimation);
	void emptyInlineRows();

	void switchToEmoji();

	void scrollToY(int y);
	void scrollUpdated();
	void disableScroll(bool dis);
	void needRefreshPanels();

	void saveConfigDelayed(int32 delay);

private:
	static constexpr bool kRefreshIconsScrollAnimation = true;
	static constexpr bool kRefreshIconsNoAnimation = false;

	struct Set {
		Set(uint64 id, MTPDstickerSet::Flags flags, const QString &title, int32 hoversSize, const StickerPack &pack = StickerPack()) : id(id), flags(flags), title(title), hovers(hoversSize, 0), pack(pack) {
		}
		uint64 id;
		MTPDstickerSet::Flags flags;
		QString title;
		QVector<float64> hovers;
		StickerPack pack;
	};
	using Sets = QList<Set>;
	Sets &shownSets() {
		return (_section == Section::Featured) ? _featuredSets : _mySets;
	}
	const Sets &shownSets() const {
		return const_cast<StickerPanInner*>(this)->shownSets();
	}
	int featuredRowHeight() const;
	void readVisibleSets();

	bool showingInlineItems() const { // Gifs or Inline results
		return (_section == Section::Inlines) || (_section == Section::Gifs);
	}

	void paintInlineItems(Painter &p, const QRect &r);
	void paintStickers(Painter &p, const QRect &r);
	void paintSticker(Painter &p, Set &set, int y, int index);
	bool featuredHasAddButton(int index) const;
	int featuredContentWidth() const;
	QRect featuredAddRect(int y) const;

	void refreshSwitchPmButton(const InlineCacheEntry *entry);

	enum class AppendSkip {
		Archived,
		Installed,
	};
	void appendSet(Sets &to, uint64 setId, AppendSkip skip);

	void selectEmoji(EmojiPtr emoji);
	QRect stickerRect(int tab, int sel);

	int32 _maxHeight;

	typedef QMap<int32, uint64> Animations; // index - showing, -index - hiding
	Animations _animations;
	Animation _a_selected;

	int _visibleTop = 0;
	int _visibleBottom = 0;

	Sets _mySets;
	Sets _featuredSets;
	OrderedSet<uint64> _installedLocallySets;
	QList<bool> _custom;

	enum class Section {
		Inlines,
		Gifs,
		Featured,
		Stickers,
	};
	Section _section = Section::Stickers;
	bool _setGifCommand = false;
	UserData *_inlineBot;
	QString _inlineBotTitle;
	uint64 _lastScrolled = 0;
	QTimer _updateInlineItems;
	bool _inlineWithThumb = false;

	std_::unique_ptr<BoxButton> _switchPmButton;
	QString _switchPmStartToken;

	typedef QVector<InlineItem*> InlineItems;
	struct InlineRow {
		InlineRow() : height(0) {
		}
		int32 height;
		InlineItems items;
	};
	typedef QVector<InlineRow> InlineRows;
	InlineRows _inlineRows;
	void clearInlineRows(bool resultsDeleted);

	using GifLayouts = QMap<DocumentData*, InlineItem*>;
	GifLayouts _gifLayouts;
	InlineItem *layoutPrepareSavedGif(DocumentData *doc, int32 position);

	using InlineLayouts = QMap<InlineResult*, InlineItem*>;
	InlineLayouts _inlineLayouts;
	InlineItem *layoutPrepareInlineResult(InlineResult *result, int32 position);

	bool inlineRowsAddItem(DocumentData *savedGif, InlineResult *result, InlineRow &row, int32 &sumWidth);
	bool inlineRowFinalize(InlineRow &row, int32 &sumWidth, bool force = false);

	InlineRow &layoutInlineRow(InlineRow &row, int32 sumWidth = 0);
	void deleteUnusedGifLayouts();

	void deleteUnusedInlineLayouts();

	int validateExistingInlineRows(const InlineResults &results);
	void selectInlineResult(int row, int column);
	void removeRecentSticker(int tab, int index);

	int _selected = -1;
	int _pressed = -1;
	int _selectedFeaturedSet = -1;
	int _pressedFeaturedSet = -1;
	int _selectedFeaturedSetAdd = -1;
	int _pressedFeaturedSetAdd = -1;
	QPoint _lastMousePos;

	QString _addText;
	int _addWidth;

	LinkButton _settings;

	QTimer _previewTimer;
	bool _previewShown = false;
};

class EmojiPanel : public TWidget {
	Q_OBJECT

public:

	EmojiPanel(QWidget *parent, const QString &text, uint64 setId, bool special, int32 wantedY); // Stickers::NoneSetId if in emoji
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

} // namespace internal

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

	void hideFast();
	bool hiding() const {
		return _hiding || _hideTimer.isActive();
	}

	void step_appearance(float64 ms, bool timer);
	void step_slide(float64 ms, bool timer);
	void step_icons(uint64 ms, bool timer);

	bool eventFilter(QObject *obj, QEvent *e);
	void stickersInstalled(uint64 setId);

	void queryInlineBot(UserData *bot, PeerData *peer, QString query);
	void clearInlineBot();

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || !_cache.isNull()) return false;

		return QRect(st::defaultDropdownPadding.left(),
					 st::defaultDropdownPadding.top(),
					 _width - st::defaultDropdownPadding.left() - st::defaultDropdownPadding.right(),
					 _height - st::defaultDropdownPadding.top() - st::defaultDropdownPadding.bottom()
					 ).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

	void notify_inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout);
	void ui_repaintInlineItem(const InlineBots::Layout::ItemBase *layout);
	bool ui_isInlineItemVisible(const InlineBots::Layout::ItemBase *layout);
	bool ui_isInlineItemBeingChosen();

	bool inlineResultsShown() const {
		return s_inner.inlineResultsShown();
	}

public slots:
	void showAnimated();
	void hideAnimated();

	void refreshStickers();

private slots:
	void refreshSavedGifs();

	void hideFinish();

	void onWndActiveChanged();

	void onTabChange();
	void onScrollEmoji();
	void onScrollStickers();
	void onSwitch();

	void onDisplaySet(quint64 setId);
	void onInstallSet(quint64 setId);
	void onRemoveSet(quint64 setId);
	void onRemoveSetSure();
	void onDelayedHide();

	void onRefreshIcons(bool scrollAnimation);
	void onRefreshPanels();

	void onSaveConfig();
	void onSaveConfigDelayed(int32 delay);

	void onInlineRequest();
	void onEmptyInlineRows();

signals:
	void emojiSelected(EmojiPtr emoji);
	void stickerSelected(DocumentData *sticker);
	void photoSelected(PhotoData *photo);
	void inlineResultSelected(InlineBots::Result *result, UserData *bot);

	void updateStickers();

private:
	bool preventAutoHide() const;
	void installSetDone(const MTPmessages_StickerSetInstallResult &result);
	bool installSetFail(uint64 setId, const RPCError &error);

	void paintStickerSettingsIcon(Painter &p) const;
	void paintFeaturedStickerSetsBadge(Painter &p, int iconLeft) const;

	enum class ValidateIconAnimations {
		Full,
		Scroll,
		None,
	};
	void validateSelectedIcon(ValidateIconAnimations animations);
	void updateContentHeight();

	void leaveToChildEvent(QEvent *e, QWidget *child);
	void startHideAnimated();
	void prepareShowHideCache();

	void updateSelected();
	void updateIcons();

	void prepareTab(int32 &left, int32 top, int32 _width, FlatRadiobutton &tab);
	void updatePanelsPositions(const QVector<internal::EmojiPanel*> &panels, int32 st);

	void showAll();
	void hideAll();

	int32 _maxHeight, _contentMaxHeight, _contentHeight, _contentHeightEmoji, _contentHeightStickers;
	bool _horizontal = false;

	bool _noTabUpdate = false;

	int32 _width, _height, _bottom;
	bool _hiding = false;
	QPixmap _cache;

	anim::fvalue a_opacity = { 0. };
	Animation _a_appearance;

	QTimer _hideTimer;

	Ui::RectShadow _shadow;

	FlatRadiobutton _recent, _people, _nature, _food, _activity, _travel, _objects, _symbols;
	QList<internal::StickerIcon> _icons;
	QVector<float64> _iconHovers;
	int _iconOver = -1;
	int _iconSel = 0;
	int _iconDown = -1;
	bool _iconsDragging = false;
	typedef QMap<int32, uint64> Animations; // index - showing, -index - hiding
	Animations _iconAnimations;
	Animation _a_icons;
	QPoint _iconsMousePos, _iconsMouseDown;
	int _iconsLeft = 0;
	int _iconsTop = 0;
	int _iconsStartX = 0;
	int _iconsMax = 0;
	anim::ivalue _iconsX = { 0, 0 };
	anim::ivalue _iconSelX = { 0, 0 };
	uint64 _iconsStartAnim = 0;

	bool _stickersShown = false;
	bool _shownFromInlineQuery = false;
	QPixmap _fromCache, _toCache;
	anim::ivalue a_fromCoord, a_toCoord;
	anim::fvalue a_fromAlpha, a_toAlpha;
	Animation _a_slide;

	ScrollArea e_scroll;
	internal::EmojiPanInner e_inner;
	QVector<internal::EmojiPanel*> e_panels;
	internal::EmojiSwitchButton e_switch;
	ScrollArea s_scroll;
	internal::StickerPanInner s_inner;
	QVector<internal::EmojiPanel*> s_panels;
	internal::EmojiSwitchButton s_switch;

	uint64 _displayingSetId = 0;
	uint64 _removingSetId = 0;

	QTimer _saveConfigTimer;

	// inline bots
	typedef QMap<QString, internal::InlineCacheEntry*> InlineCache;
	InlineCache _inlineCache;
	QTimer _inlineRequestTimer;

	void inlineBotChanged();
	int32 showInlineRows(bool newResults);
	bool hideOnNoInlineResults();
	void recountContentMaxHeight();
	bool refreshInlineRows(int32 *added = 0);
	UserData *_inlineBot = nullptr;
	PeerData *_inlineQueryPeer = nullptr;
	QString _inlineQuery, _inlineNextQuery, _inlineNextOffset;
	mtpRequestId _inlineRequestId = 0;
	void inlineResultsDone(const MTPmessages_BotResults &result);
	bool inlineResultsFail(const RPCError &error);

};
