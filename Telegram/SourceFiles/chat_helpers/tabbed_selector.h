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
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace SendMenu {
enum class Type;
} // namespace SendMenu

namespace ChatHelpers {

enum class SelectorTab {
	Emoji,
	Stickers,
	Gifs,
};

class EmojiListWidget;
class StickersListWidget;
class GifsListWidget;

class TabbedSelector : public Ui::RpWidget, private base::Subscriber {
public:
	struct FileChosen {
		not_null<DocumentData*> document;
		Api::SendOptions options;
	};
	struct PhotoChosen {
		not_null<PhotoData*> photo;
		Api::SendOptions options;
	};
	using InlineChosen = InlineBots::ResultSelected;
	enum class Mode {
		Full,
		EmojiOnly
	};

	TabbedSelector(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Mode mode = Mode::Full);
	~TabbedSelector();

	Main::Session &session() const;

	rpl::producer<EmojiPtr> emojiChosen() const;
	rpl::producer<FileChosen> fileChosen() const;
	rpl::producer<PhotoChosen> photoChosen() const;
	rpl::producer<InlineChosen> inlineResultChosen() const;

	rpl::producer<> cancelled() const;
	rpl::producer<> checkForHide() const;
	rpl::producer<> slideFinished() const;

	void setRoundRadius(int radius);
	void refreshStickers();
	void setCurrentPeer(PeerData *peer);

	void hideFinished();
	void showStarted();
	void beforeHiding();
	void afterShown();

	int marginTop() const;
	int marginBottom() const;
	int scrollTop() const;

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

	void setSendMenuType(Fn<SendMenu::Type()> &&callback) {
		_sendMenuType = std::move(callback);
	}

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
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	class Tab {
	public:
		static constexpr auto kCount = 3;

		Tab(SelectorTab type, object_ptr<Inner> widget);

		object_ptr<Inner> takeWidget();
		void returnWidget(object_ptr<Inner> widget);

		SelectorTab type() const {
			return _type;
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
		SelectorTab _type = SelectorTab::Emoji;
		object_ptr<Inner> _widget = { nullptr };
		QPointer<Inner> _weak;
		object_ptr<InnerFooter> _footer;
		int _scrollTop = 0;

	};

	bool full() const;
	Tab createTab(SelectorTab type);

	void paintSlideFrame(Painter &p);
	void paintContent(Painter &p);

	void checkRestrictedPeer();
	bool isRestrictedView();
	void updateRestrictedLabelGeometry();
	void handleScroll();

	QImage grabForAnimation();

	void scrollToY(int y);

	void showAll();
	void hideForSliding();

	bool hasSectionIcons() const;
	void setWidgetToScrollArea();
	void createTabsSlider();
	void switchTab();
	not_null<Tab*> getTab(SelectorTab type) {
		return &_tabs[static_cast<int>(type)];
	}
	not_null<const Tab*> getTab(SelectorTab type) const {
		return &_tabs[static_cast<int>(type)];
	}
	not_null<Tab*> currentTab() {
		return getTab(_currentTabType);
	}
	not_null<const Tab*> currentTab() const {
		return getTab(_currentTabType);
	}
	not_null<EmojiListWidget*> emoji() const;
	not_null<StickersListWidget*> stickers() const;
	not_null<GifsListWidget*> gifs() const;

	const not_null<Window::SessionController*> _controller;

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
	std::array<Tab, Tab::kCount> _tabs;
	SelectorTab _currentTabType = SelectorTab::Emoji;

	base::unique_qptr<Ui::PopupMenu> _menu;

	Fn<void(SelectorTab)> _afterShownCallback;
	Fn<void(SelectorTab)> _beforeHidingCallback;

	Fn<SendMenu::Type()> _sendMenuType;

	rpl::event_stream<> _showRequests;
	rpl::event_stream<> _slideFinished;

};

class TabbedSelector::Inner : public Ui::RpWidget {
public:
	Inner(QWidget *parent, not_null<Window::SessionController*> controller);

	not_null<Window::SessionController*> controller() const {
		return _controller;
	}

	int getVisibleTop() const {
		return _visibleTop;
	}
	int getVisibleBottom() const {
		return _visibleBottom;
	}
	void setMinimalHeight(int newWidth, int newMinimalHeight);

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
	virtual void fillContextMenu(
		not_null<Ui::PopupMenu*> menu,
		SendMenu::Type type) {
	}

	rpl::producer<int> scrollToRequests() const;
	rpl::producer<bool> disableScrollRequests() const;

	virtual object_ptr<InnerFooter> createFooter() = 0;

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	int minimalHeight() const;
	int resizeGetHeight(int newWidth) override final;

	virtual int countDesiredHeight(int newWidth) = 0;
	virtual InnerFooter *getFooter() const = 0;
	virtual void processHideFinished() {
	}
	virtual void processPanelHideFinished() {
	}

	void scrollTo(int y);
	void disableScroll(bool disabled);

private:
	not_null<Window::SessionController*> _controller;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _minimalHeight = 0;

	rpl::event_stream<int> _scrollToRequests;
	rpl::event_stream<bool> _disableScrollRequests;

};

class TabbedSelector::InnerFooter : public Ui::RpWidget {
public:
	InnerFooter(QWidget *parent);

protected:
	virtual void processHideFinished() {
	}
	virtual void processPanelHideFinished() {
	}
	friend class Inner;

};

} // namespace ChatHelpers
