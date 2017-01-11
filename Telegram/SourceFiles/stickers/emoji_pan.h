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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/twidget.h"
#include "ui/abstract_button.h"
#include "ui/effects/panel_animation.h"

namespace InlineBots {
namespace Layout {
class ItemBase;
} // namespace Layout
class Result;
} // namespace InlineBots

namespace Ui {
class ScrollArea;
class IconButton;
class LinkButton;
class RoundButton;
class RippleAnimation;
} // namesapce Ui

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
	EmojiColorPicker(QWidget *parent);

	void showEmoji(uint32 code);

	void clearSelection();
	void handleMouseMove(QPoint globalPos);
	void handleMouseRelease(QPoint globalPos);

	void hideFast();

public slots:
	void showAnimated();
	void hideAnimated();

signals:
	void emojiSelected(EmojiPtr emoji);
	void hidden();

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void animationCallback();

	void drawVariant(Painter &p, int variant);

	void updateSelected();
	void setSelected(int newSelected);

	bool _ignoreShow = false;

	EmojiPtr _variants[EmojiColorsCount + 1];

	int _selected = -1;
	int _pressedSel = -1;
	QPoint _lastMousePos;

	bool _hiding = false;
	QPixmap _cache;
	Animation _a_opacity;

	QTimer _hideTimer;

};

class EmojiPanel;
class EmojiPanInner : public TWidget {
	Q_OBJECT

public:
	EmojiPanInner(QWidget *parent);

	void setMaxHeight(int maxHeight);

	void hideFinish();

	void showEmojiPack(DBIEmojiTab packIndex);

	void clearSelection();

	DBIEmojiTab currentTab(int yOffset) const;

	void refreshRecent();

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	void fillPanels(QVector<EmojiPanel*> &panels);
	void refreshPanels(QVector<EmojiPanel*> &panels);

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;

public slots:
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
	void updateSelected();
	void setSelected(int newSelected);

	int32 _maxHeight;

	int countHeight();
	void selectEmoji(EmojiPtr emoji);

	QRect emojiRect(int tab, int sel);

	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _counts[emojiTabCount];

	QVector<EmojiPtr> _emojis[emojiTabCount];

	int32 _esize;

	int _selected = -1;
	int _pressedSel = -1;
	int _pickerSel = -1;
	QPoint _lastMousePos;

	object_ptr<EmojiColorPicker> _picker;
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
	StickerPanInner(QWidget *parent);

	void setMaxHeight(int maxHeight);

	void hideFinish(bool completely);
	void showFinish();
	void showStickerSet(uint64 setId);
	void updateShowingSavedGifs();

	bool showSectionIcons() const;
	void clearSelection();

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
	void paintEvent(QPaintEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;

private slots:
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

	void updateSelected();
	void setSelected(int newSelected, int newSelectedFeaturedSet, int newSelectedFeaturedSetAdd);

	void setPressedFeaturedSetAdd(int newPressedFeaturedSetAdd);

	struct Set {
		Set(uint64 id, MTPDstickerSet::Flags flags, const QString &title, int32 hoversSize, const StickerPack &pack = StickerPack()) : id(id), flags(flags), title(title), pack(pack) {
		}
		uint64 id;
		MTPDstickerSet::Flags flags;
		QString title;
		StickerPack pack;
		QSharedPointer<Ui::RippleAnimation> ripple;
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
	void paintSticker(Painter &p, Set &set, int y, int index, bool selected, bool deleteSelected);
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
	int stickersLeft() const;
	QRect stickerRect(int tab, int sel);

	int32 _maxHeight;

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
	TimeMs _lastScrolled = 0;
	QTimer _updateInlineItems;
	bool _inlineWithThumb = false;

	object_ptr<Ui::RoundButton> _switchPmButton = { nullptr };
	QString _switchPmStartToken;

	typedef QVector<InlineItem*> InlineItems;
	struct InlineRow {
		int height = 0;
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

	object_ptr<Ui::LinkButton> _settings;

	QTimer _previewTimer;
	bool _previewShown = false;
};

class EmojiPanel : public TWidget {
	Q_OBJECT

public:
	EmojiPanel(QWidget *parent, const QString &text, uint64 setId, bool special, int32 wantedY); // Stickers::NoneSetId if in emoji
	void setText(const QString &text);
	void setDeleteVisible(bool isVisible);

	int wantedY() const {
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

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void updateText();

	int32 _wantedY;
	QString _text, _fullText;
	uint64 _setId;
	bool _special, _deleteVisible;
	Ui::IconButton *_delete = nullptr;

};

class EmojiSwitchButton : public Ui::AbstractButton {
public:
	EmojiSwitchButton(QWidget *parent, bool toStickers); // otherwise toEmoji
	void updateText(const QString &inlineBotUsername = QString());

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	bool _toStickers = false;
	QString _text;
	int _textWidth = 0;

};

} // namespace internal

class EmojiPan : public TWidget, public RPCSender {
	Q_OBJECT

public:
	EmojiPan(QWidget *parent);

	void setMinTop(int minTop);
	void setMinBottom(int minBottom);
	void moveBottom(int bottom);

	void hideFast();
	bool hiding() const {
		return _hiding || _hideTimer.isActive();
	}

	void step_icons(TimeMs ms, bool timer);

	void leaveToChildEvent(QEvent *e, QWidget *child) override;

	void stickersInstalled(uint64 setId);

	void queryInlineBot(UserData *bot, PeerData *peer, QString query);
	void clearInlineBot();

	bool overlaps(const QRect &globalRect) const;

	void notify_inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout);
	void ui_repaintInlineItem(const InlineBots::Layout::ItemBase *layout);
	bool ui_isInlineItemVisible(const InlineBots::Layout::ItemBase *layout);
	bool ui_isInlineItemBeingChosen();

	void setOrigin(Ui::PanelAnimation::Origin origin);
	void showAnimated(Ui::PanelAnimation::Origin origin);
	void hideAnimated();

	~EmojiPan();

protected:
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void otherEnter();
	void otherLeave();

	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	bool event(QEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

public slots:
	void refreshStickers();

private slots:
	void hideByTimerOrLeave();
	void refreshSavedGifs();

	void onWndActiveChanged();

	void onScrollEmoji();
	void onScrollStickers();
	void onSwitch();

	void onDisplaySet(quint64 setId);
	void onInstallSet(quint64 setId);
	void onRemoveSet(quint64 setId);
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
	bool inlineResultsShown() const;
	int countBottom() const;
	void moveByBottom();
	void paintContent(Painter &p);
	void performSwitch();

	style::margins innerPadding() const;

	// Rounded rect which has shadow around it.
	QRect innerRect() const;

	// Inner rect with removed st::buttonRadius from top and bottom.
	// This one is allowed to be not rounded.
	QRect horizontalRect() const;

	// Inner rect with removed st::buttonRadius from left and right.
	// This one is allowed to be not rounded.
	QRect verticalRect() const;

	QImage grabForPanelAnimation();
	void startShowAnimation();
	void startOpacityAnimation(bool hiding);
	void prepareCache();

	class Container;
	void opacityAnimationCallback();

	void hideFinished();
	void showStarted();

	bool preventAutoHide() const;
	void installSetDone(const MTPmessages_StickerSetInstallResult &result);
	bool installSetFail(uint64 setId, const RPCError &error);
	void setActiveTab(DBIEmojiTab tab);
	void setCurrentTabIcon(DBIEmojiTab tab);

	void paintStickerSettingsIcon(Painter &p) const;
	void paintFeaturedStickerSetsBadge(Painter &p, int iconLeft) const;

	enum class ValidateIconAnimations {
		Full,
		Scroll,
		None,
	};
	void validateSelectedIcon(ValidateIconAnimations animations);
	void updateContentHeight();

	void updateSelected();
	void updateIcons();

	void prepareTab(int &left, int top, int _width, Ui::IconButton *tab, DBIEmojiTab value);
	void updatePanelsPositions(const QVector<internal::EmojiPanel*> &panels, int st);

	void showAll();
	void hideAll();

	int _minTop = 0;
	int _minBottom = 0;
	int _contentMaxHeight = 0;
	int _contentHeight = 0;
	int _contentHeightEmoji = 0;
	int _contentHeightStickers = 0;
	bool _horizontal = false;

	int _width = 0;
	int _height = 0;
	int _bottom = 0;

	Ui::PanelAnimation::Origin _origin = Ui::PanelAnimation::Origin::BottomRight;
	std_::unique_ptr<Ui::PanelAnimation> _showAnimation;
	Animation _a_show;

	bool _hiding = false;
	QPixmap _cache;
	Animation _a_opacity;
	QTimer _hideTimer;
	bool _inPanelGrab = false;

	class SlideAnimation;
	std_::unique_ptr<SlideAnimation> _slideAnimation;
	Animation _a_slide;

	object_ptr<Ui::IconButton> _recent;
	object_ptr<Ui::IconButton> _people;
	object_ptr<Ui::IconButton> _nature;
	object_ptr<Ui::IconButton> _food;
	object_ptr<Ui::IconButton> _activity;
	object_ptr<Ui::IconButton> _travel;
	object_ptr<Ui::IconButton> _objects;
	object_ptr<Ui::IconButton> _symbols;

	QList<internal::StickerIcon> _icons;
	int _iconOver = -1;
	int _iconSel = 0;
	int _iconDown = -1;
	bool _iconsDragging = false;
	BasicAnimation _a_icons;
	QPoint _iconsMousePos, _iconsMouseDown;
	int _iconsLeft = 0;
	int _iconsTop = 0;
	int _iconsStartX = 0;
	int _iconsMax = 0;
	anim::value _iconsX;
	anim::value _iconSelX;
	TimeMs _iconsStartAnim = 0;

	bool _emojiShown = true;
	bool _shownFromInlineQuery = false;

	object_ptr<Ui::ScrollArea> e_scroll;
	QPointer<internal::EmojiPanInner> e_inner;
	QVector<internal::EmojiPanel*> e_panels;
	object_ptr<internal::EmojiSwitchButton> e_switch;
	object_ptr<Ui::ScrollArea> s_scroll;
	QPointer<internal::StickerPanInner> s_inner;
	QVector<internal::EmojiPanel*> s_panels;
	object_ptr<internal::EmojiSwitchButton> s_switch;

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
