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
#include "mtproto/sender.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "auth_session.h"

namespace InlineBots {
namespace Layout {
class ItemBase;
} // namespace Layout
class Result;
} // namespace InlineBots

namespace Ui {
class PlainShadow;
class ScrollArea;
class IconButton;
class LinkButton;
class RoundButton;
class RippleAnimation;
class SettingsSlider;
} // namesapce Ui

namespace internal {

constexpr auto kInlineItemsMaxPerRow = 5;

constexpr auto kEmojiSectionCount = 8;
inline DBIEmojiSection EmojiSectionAtIndex(int index) {
	return (index < 0 || index >= kEmojiSectionCount) ? dbiesRecent : DBIEmojiSection(index - 1);
}

using InlineResult = InlineBots::Result;
using InlineResults = std::vector<std::unique_ptr<InlineResult>>;
using InlineItem = InlineBots::Layout::ItemBase;

struct InlineCacheEntry {
	QString nextOffset;
	QString switchPmText, switchPmStartToken;
	InlineResults results;
};

class EmojiColorPicker : public TWidget {
	Q_OBJECT

public:
	EmojiColorPicker(QWidget *parent);

	void showEmoji(EmojiPtr emoji);

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
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void animationCallback();

	void drawVariant(Painter &p, int variant);

	void updateSelected();
	void setSelected(int newSelected);

	bool _ignoreShow = false;

	QVector<EmojiPtr> _variants;

	int _selected = -1;
	int _pressedSel = -1;
	QPoint _lastMousePos;

	bool _hiding = false;
	QPixmap _cache;
	Animation _a_opacity;

	QTimer _hideTimer;

};

class BasicPanInner : public TWidget {
	Q_OBJECT

public:
	BasicPanInner(QWidget *parent);

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	int getVisibleTop() const {
		return _visibleTop;
	}
	int getVisibleBottom() const {
		return _visibleBottom;
	}

	virtual void refreshRecent() = 0;
	virtual void preloadImages() {
	}
	virtual void hideFinish(bool completely) = 0;
	virtual void clearSelection() = 0;

	virtual object_ptr<TWidget> createController() = 0;

signals:
	void scrollToY(int y);
	void disableScroll(bool disabled);
	void saveConfigDelayed(int delay);

protected:
	virtual int countHeight() = 0;

private:
	int _visibleTop = 0;
	int _visibleBottom = 0;

};

class EmojiPanInner : public BasicPanInner {
	Q_OBJECT

public:
	EmojiPanInner(QWidget *parent);

	void refreshRecent() override;
	void hideFinish(bool completely) override;
	void clearSelection() override;
	object_ptr<TWidget> createController() override;

	void showEmojiSection(DBIEmojiSection section);
	DBIEmojiSection currentSection(int yOffset) const;

public slots:
	void onShowPicker();
	void onPickerHidden();
	void onColorSelected(EmojiPtr emoji);

	bool checkPickerHide();

signals:
	void selected(EmojiPtr emoji);
	void switchToStickers();

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;
	bool event(QEvent *e) override;
	int countHeight() override;

private:
	class Controller;

	struct SectionInfo {
		int section = 0;
		int count = 0;
		int top = 0;
		int rowsCount = 0;
		int rowsTop = 0;
		int rowsBottom = 0;
	};
	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	SectionInfo sectionInfo(int section) const;
	SectionInfo sectionInfoByOffset(int yOffset) const;

	void ensureLoaded(int section);
	int countSectionTop(int section) const;
	void updateSelected();
	void setSelected(int newSelected);

	void selectEmoji(EmojiPtr emoji);

	QRect emojiRect(int section, int sel);

	int _counts[kEmojiSectionCount];
	QVector<EmojiPtr> _emoji[kEmojiSectionCount];

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
	uint64 setId = 0;
	DocumentData *sticker = nullptr;
	int pixw = 0;
	int pixh = 0;

};

class StickerPanInner : public BasicPanInner, public InlineBots::Layout::Context, private base::Subscriber {
	Q_OBJECT

public:
	StickerPanInner(QWidget *parent, bool gifs);

	void refreshRecent() override;
	void preloadImages() override;
	void hideFinish(bool completely) override;
	void clearSelection() override;
	object_ptr<TWidget> createController() override;

	void showStickerSet(uint64 setId);

	void refreshStickers();
	void refreshRecentStickers(bool resize = true);
	void refreshSavedGifs();
	int refreshInlineRows(UserData *bot, const InlineCacheEntry *results, bool resultsDeleted);
	void inlineBotChanged();
	void hideInlineRowsPanel();
	void clearInlineRowsPanel();

	void fillIcons(QList<StickerIcon> &icons);

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	uint64 currentSet(int yOffset) const;

	void inlineItemLayoutChanged(const InlineItem *layout) override;
	void inlineItemRepaint(const InlineItem *layout) override;
	bool inlineItemVisible(const InlineItem *layout) override;

	void installedLocally(uint64 setId);
	void notInstalledLocally(uint64 setId);
	void clearInstalledLocally();

	~StickerPanInner();

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;
	int countHeight() override;

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
	void scrollUpdated();

private:
	enum class Section {
		Inlines,
		Gifs,
		Featured,
		Stickers,
	};
	class Controller;

	static constexpr auto kRefreshIconsScrollAnimation = true;
	static constexpr auto kRefreshIconsNoAnimation = false;

	struct SectionInfo {
		int section = 0;
		int count = 0;
		int top = 0;
		int rowsCount = 0;
		int rowsTop = 0;
		int rowsBottom = 0;
	};
	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	SectionInfo sectionInfo(int section) const;
	SectionInfo sectionInfoByOffset(int yOffset) const;

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

	void paintInlineItems(Painter &p, QRect clip);
	void paintFeaturedStickers(Painter &p, QRect clip);
	void paintStickers(Painter &p, QRect clip);
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
	QRect stickerRect(int section, int sel);

	Sets _mySets;
	Sets _featuredSets;
	OrderedSet<uint64> _installedLocallySets;
	QList<bool> _custom;

	Section _section = Section::Stickers;
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

	std::map<DocumentData*, std::unique_ptr<InlineItem>> _gifLayouts;
	InlineItem *layoutPrepareSavedGif(DocumentData *doc, int32 position);

	std::map<InlineResult*, std::unique_ptr<InlineItem>> _inlineLayouts;
	InlineItem *layoutPrepareInlineResult(InlineResult *result, int32 position);

	bool inlineRowsAddItem(DocumentData *savedGif, InlineResult *result, InlineRow &row, int32 &sumWidth);
	bool inlineRowFinalize(InlineRow &row, int32 &sumWidth, bool force = false);

	InlineRow &layoutInlineRow(InlineRow &row, int32 sumWidth = 0);
	void deleteUnusedGifLayouts();

	void deleteUnusedInlineLayouts();

	int validateExistingInlineRows(const InlineResults &results);
	void selectInlineResult(int row, int column);
	void removeRecentSticker(int section, int index);

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

} // namespace internal

class EmojiPanel : public TWidget, private MTP::Sender {
	Q_OBJECT

public:
	EmojiPanel(QWidget *parent);

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

	bool ui_isInlineItemBeingChosen();

	void showAnimated();
	void hideAnimated();

	~EmojiPanel();

protected:
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
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
	void onScroll();

	void onDisplaySet(quint64 setId);
	void onInstallSet(quint64 setId);
	void onRemoveSet(quint64 setId);
	void onDelayedHide();

	void onRefreshIcons(bool scrollAnimation);

	void onSaveConfig();
	void onSaveConfigDelayed(int delay);

	void onInlineRequest();
	void onEmptyInlineRows();

signals:
	void emojiSelected(EmojiPtr emoji);
	void stickerSelected(DocumentData *sticker);
	void photoSelected(PhotoData *photo);
	void inlineResultSelected(InlineBots::Result *result, UserData *bot);

	void updateStickers();

private:
	using TabType = EmojiPanelTab;
	class Tab {
	public:
		static constexpr auto kCount = 3;

		Tab(TabType type, object_ptr<internal::BasicPanInner> widget);

		object_ptr<internal::BasicPanInner> takeWidget();
		void returnWidget(object_ptr<internal::BasicPanInner> widget);

		TabType type() const {
			return _type;
		}
		gsl::not_null<internal::BasicPanInner*> widget() const {
			return _weak;
		}

		void saveScrollTop();
		void saveScrollTop(int scrollTop) {
			_scrollTop = scrollTop;
		}
		int getScrollTop() const {
			return _scrollTop;
		}

	private:
		TabType _type = TabType::Emoji;
		object_ptr<internal::BasicPanInner> _widget = { nullptr };
		QPointer<internal::BasicPanInner> _weak;
		int _scrollTop = 0;

	};

	int marginTop() const;
	int marginBottom() const;
	int countBottom() const;
	void moveByBottom();
	void paintSlideFrame(Painter &p, TimeMs ms);
	void paintContent(Painter &p);

	style::margins innerPadding() const;

	// Rounded rect which has shadow around it.
	QRect innerRect() const;

	// Inner rect with removed st::buttonRadius from top and bottom.
	// This one is allowed to be not rounded.
	QRect horizontalRect() const;

	// Inner rect with removed st::buttonRadius from left and right.
	// This one is allowed to be not rounded.
	QRect verticalRect() const;

	enum class GrabType {
		Panel,
		Slide,
	};
	QImage grabForComplexAnimation(GrabType type);
	void startShowAnimation();
	void startOpacityAnimation(bool hiding);
	void prepareCache();

	void opacityAnimationCallback();

	void hideFinished();
	void showStarted();

	bool preventAutoHide() const;
	void setActiveSection(DBIEmojiSection section);
	void setCurrentSectionIcon(DBIEmojiSection section);

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

	void prepareSection(int &left, int top, int _width, Ui::IconButton *sectionIcon, DBIEmojiSection section);

	void showAll();
	void hideForSliding();

	void setWidgetToScrollArea();
	void createTabsSlider();
	void switchTab();
	gsl::not_null<Tab*> getTab(TabType type) {
		return &_tabs[static_cast<int>(type)];
	}
	gsl::not_null<const Tab*> getTab(TabType type) const {
		return &_tabs[static_cast<int>(type)];
	}
	gsl::not_null<Tab*> currentTab() {
		return getTab(_currentTabType);
	}
	gsl::not_null<const Tab*> currentTab() const {
		return getTab(_currentTabType);
	}
	gsl::not_null<internal::EmojiPanInner*> emoji() const {
		return static_cast<internal::EmojiPanInner*>(getTab(TabType::Emoji)->widget().get());
	}
	gsl::not_null<internal::StickerPanInner*> stickers() const {
		return static_cast<internal::StickerPanInner*>(getTab(TabType::Stickers)->widget().get());
	}
	gsl::not_null<internal::StickerPanInner*> gifs() const {
		return static_cast<internal::StickerPanInner*>(getTab(TabType::Gifs)->widget().get());
	}

	int _minTop = 0;
	int _minBottom = 0;
	int _contentMaxHeight = 0;
	int _contentHeight = 0;
	bool _horizontal = false;

	int _width = 0;
	int _height = 0;
	int _bottom = 0;

	std::unique_ptr<Ui::PanelAnimation> _showAnimation;
	Animation _a_show;

	bool _hiding = false;
	bool _hideAfterSlide = false;
	QPixmap _cache;
	Animation _a_opacity;
	QTimer _hideTimer;
	bool _inComplrexGrab = false;

	class SlideAnimation;
	std::unique_ptr<SlideAnimation> _slideAnimation;
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

	object_ptr<Ui::SettingsSlider> _tabsSlider = { nullptr };
	object_ptr<Ui::PlainShadow> _topShadow;
	object_ptr<Ui::PlainShadow> _bottomShadow;
	object_ptr<Ui::ScrollArea> _scroll;
	std::array<Tab, Tab::kCount> _tabs;
	TabType _currentTabType = TabType::Emoji;

	uint64 _displayingSetId = 0;
	uint64 _removingSetId = 0;

	QTimer _saveConfigTimer;

	// inline bots
	std::map<QString, std::unique_ptr<internal::InlineCacheEntry>> _inlineCache;
	QTimer _inlineRequestTimer;

	void inlineBotChanged();
	int32 showInlineRows(bool newResults);
	bool refreshInlineRows(int32 *added = 0);
	UserData *_inlineBot = nullptr;
	PeerData *_inlineQueryPeer = nullptr;
	QString _inlineQuery, _inlineNextQuery, _inlineNextOffset;
	mtpRequestId _inlineRequestId = 0;
	void inlineResultsDone(const MTPmessages_BotResults &result);

};
