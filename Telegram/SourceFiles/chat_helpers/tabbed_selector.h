/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/effects/message_sending_animation_common.h"
#include "ui/effects/panel_animation.h"
#include "mtproto/sender.h"
#include "base/object_ptr.h"

namespace InlineBots {
struct ResultSelected;
} // namespace InlineBots

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PlainShadow;
class PopupMenu;
class ScrollArea;
class SettingsSlider;
class FlatLabel;
class BoxContent;
} // namespace Ui

namespace Window {
class SessionController;
enum class GifPauseReason;
} // namespace Window

namespace SendMenu {
enum class Type;
} // namespace SendMenu

namespace style {
struct EmojiPan;
} // namespace style

namespace ChatHelpers {

enum class SelectorTab {
	Emoji,
	Stickers,
	Gifs,
	Masks,
};

class EmojiListWidget;
class StickersListWidget;
class GifsListWidget;

class TabbedSelector : public Ui::RpWidget {
public:
	static constexpr auto kPickCustomTimeId = -1;
	struct FileChosen {
		not_null<DocumentData*> document;
		Api::SendOptions options;
		Ui::MessageSendingAnimationFrom messageSendingFrom;
	};
	struct PhotoChosen {
		not_null<PhotoData*> photo;
		Api::SendOptions options;
	};
	using InlineChosen = InlineBots::ResultSelected;
	enum class Mode {
		Full,
		EmojiOnly,
		MediaEditor,
		EmojiStatus,
	};
	enum class Action {
		Update,
		Cancel,
	};

	TabbedSelector(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::GifPauseReason level,
		Mode mode = Mode::Full);
	~TabbedSelector();

	Main::Session &session() const;
	Window::GifPauseReason level() const;

	rpl::producer<EmojiPtr> emojiChosen() const;
	rpl::producer<FileChosen> customEmojiChosen() const;
	rpl::producer<not_null<DocumentData*>> premiumEmojiChosen() const;
	rpl::producer<FileChosen> fileChosen() const;
	rpl::producer<PhotoChosen> photoChosen() const;
	rpl::producer<InlineChosen> inlineResultChosen() const;

	rpl::producer<> cancelled() const;
	rpl::producer<> checkForHide() const;
	rpl::producer<> slideFinished() const;
	rpl::producer<> contextMenuRequested() const;
	rpl::producer<Action> choosingStickerUpdated() const;

	void setAllowEmojiWithoutPremium(bool allow);
	void setRoundRadius(int radius);
	void refreshStickers();
	void setCurrentPeer(PeerData *peer);
	void showPromoForPremiumEmoji();
	void provideRecentEmoji(const std::vector<DocumentId> &customRecentList);

	void hideFinished();
	void showStarted();
	void beforeHiding();
	void afterShown();

	[[nodiscard]] int marginTop() const;
	[[nodiscard]] int marginBottom() const;
	[[nodiscard]] int scrollTop() const;
	[[nodiscard]] int scrollBottom() const;

	bool preventAutoHide() const;
	bool isSliding() const {
		return _a_slide.animating();
	}
	bool hasMenu() const;

	void setAfterShownCallback(Fn<void(SelectorTab)> callback) {
		_afterShownCallback = std::move(callback);
	}
	void setBeforeHidingCallback(Fn<void(SelectorTab)> callback) {
		_beforeHidingCallback = std::move(callback);
	}

	void showMenuWithType(SendMenu::Type type);
	void setDropDown(bool dropDown);

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e);
	QRect floatPlayerAvailableRect() const;

	auto showRequests() const {
		return _showRequests.events();
	}

	class Inner;
	class InnerFooter;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	class Tab {
	public:
		Tab(SelectorTab type, int index, object_ptr<Inner> widget);

		object_ptr<Inner> takeWidget();
		void returnWidget(object_ptr<Inner> widget);

		SelectorTab type() const {
			return _type;
		}
		int index() const {
			return _index;
		}
		Inner *widget() const {
			return _weak;
		}
		not_null<InnerFooter*> footer() const {
			return _footer;
		}

		void saveScrollTop();
		void saveScrollTop(int scrollTop) {
			_scrollTop = scrollTop;
		}
		int getScrollTop() const {
			return _scrollTop;
		}

	private:
		const SelectorTab _type;
		const int _index;
		object_ptr<Inner> _widget = { nullptr };
		QPointer<Inner> _weak;
		object_ptr<InnerFooter> _footer;
		int _scrollTop = 0;

	};

	bool full() const;
	bool mediaEditor() const;
	bool tabbed() const;
	bool hasEmojiTab() const;
	bool hasStickersTab() const;
	bool hasGifsTab() const;
	bool hasMasksTab() const;
	Tab createTab(SelectorTab type, int index);

	void paintSlideFrame(Painter &p);
	void paintBgRoundedPart(Painter &p);
	void paintContent(Painter &p);

	void checkRestrictedPeer();
	bool isRestrictedView();
	void updateRestrictedLabelGeometry();
	void updateScrollGeometry(QSize oldSize);
	void updateFooterGeometry();
	void handleScroll();

	QImage grabForAnimation();

	void scrollToY(int y);

	void showAll();
	void hideForSliding();

	SelectorTab typeByIndex(int index) const;
	int indexByType(SelectorTab type) const;

	bool hasSectionIcons() const;
	void setWidgetToScrollArea();
	void createTabsSlider();
	void fillTabsSliderSections();
	void updateTabsSliderGeometry();
	void switchTab();

	not_null<Tab*> getTab(int index);
	not_null<const Tab*> getTab(int index) const;
	not_null<Tab*> currentTab();
	not_null<const Tab*> currentTab() const;

	not_null<EmojiListWidget*> emoji() const;
	not_null<StickersListWidget*> stickers() const;
	not_null<GifsListWidget*> gifs() const;
	not_null<StickersListWidget*> masks() const;

	const style::EmojiPan &_st;
	const not_null<Window::SessionController*> _controller;
	const Window::GifPauseReason _level = {};

	Mode _mode = Mode::Full;
	int _roundRadius = 0;
	int _footerTop = 0;
	PeerData *_currentPeer = nullptr;

	class SlideAnimation;
	std::unique_ptr<SlideAnimation> _slideAnimation;
	Ui::Animations::Simple _a_slide;

	object_ptr<Ui::SettingsSlider> _tabsSlider = { nullptr };
	object_ptr<Ui::PlainShadow> _topShadow;
	object_ptr<Ui::PlainShadow> _bottomShadow;
	object_ptr<Ui::ScrollArea> _scroll;
	object_ptr<Ui::FlatLabel> _restrictedLabel = { nullptr };
	std::vector<Tab> _tabs;
	SelectorTab _currentTabType = SelectorTab::Emoji;

	const bool _hasEmojiTab;
	const bool _hasStickersTab;
	const bool _hasGifsTab;
	const bool _hasMasksTab;
	const bool _tabbed;
	bool _dropDown = false;

	base::unique_qptr<Ui::PopupMenu> _menu;

	Fn<void(SelectorTab)> _afterShownCallback;
	Fn<void(SelectorTab)> _beforeHidingCallback;

	rpl::event_stream<> _showRequests;
	rpl::event_stream<> _slideFinished;

};

class TabbedSelector::Inner : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::GifPauseReason level);
	Inner(
		QWidget *parent,
		const style::EmojiPan &st,
		not_null<Main::Session*> session,
		Fn<bool()> paused);

	[[nodiscard]] Main::Session &session() const {
		return *_session;
	}
	[[nodiscard]] const style::EmojiPan &st() const {
		return _st;
	}
	[[nodiscard]] Fn<bool()> pausedMethod() const {
		return _paused;
	}
	[[nodiscard]] bool paused() const {
		return _paused();
	}

	[[nodiscard]] int getVisibleTop() const {
		return _visibleTop;
	}
	[[nodiscard]] int getVisibleBottom() const {
		return _visibleBottom;
	}
	void setMinimalHeight(int newWidth, int newMinimalHeight);

	[[nodiscard]] rpl::producer<> checkForHide() const {
		return _checkForHide.events();
	}
	[[nodiscard]] bool preventAutoHide() const {
		return _preventHideWithBox;
	}

	virtual void refreshRecent() = 0;
	virtual void preloadImages() {
	}
	void hideFinished();
	void panelHideFinished();
	virtual void clearSelection() = 0;

	virtual void afterShown() {
	}
	virtual void beforeHiding() {
	}
	[[nodiscard]] virtual base::unique_qptr<Ui::PopupMenu> fillContextMenu(
			SendMenu::Type type) {
		return nullptr;
	}

	rpl::producer<int> scrollToRequests() const;
	rpl::producer<bool> disableScrollRequests() const;

	virtual object_ptr<InnerFooter> createFooter() = 0;

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	int minimalHeight() const;
	virtual int defaultMinimalHeight() const;
	int resizeGetHeight(int newWidth) override final;

	virtual int countDesiredHeight(int newWidth) = 0;
	virtual InnerFooter *getFooter() const = 0;
	virtual void processHideFinished() {
	}
	virtual void processPanelHideFinished() {
	}

	void scrollTo(int y);
	void disableScroll(bool disabled);

	void checkHideWithBox(QPointer<Ui::BoxContent> box);

private:
	const style::EmojiPan &_st;
	const not_null<Main::Session*> _session;
	const Fn<bool()> _paused;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _minimalHeight = 0;

	rpl::event_stream<int> _scrollToRequests;
	rpl::event_stream<bool> _disableScrollRequests;
	rpl::event_stream<> _checkForHide;

	bool _preventHideWithBox = false;

};

class TabbedSelector::InnerFooter : public Ui::RpWidget {
public:
	InnerFooter(QWidget *parent, const style::EmojiPan &st);

	[[nodiscard]] const style::EmojiPan &st() const;

protected:
	virtual void processHideFinished() {
	}
	virtual void processPanelHideFinished() {
	}
	friend class Inner;

private:
	const style::EmojiPan &_st;

};

} // namespace ChatHelpers
