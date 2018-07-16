/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/panel_animation.h"
#include "mtproto/sender.h"
#include "auth_session.h"

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace Ui {
class PlainShadow;
class ScrollArea;
class SettingsSlider;
class FlatLabel;
} // namesapce Ui

namespace Window {
class Controller;
} // namespace Window

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
	Q_OBJECT

public:
	TabbedSelector(QWidget *parent, not_null<Window::Controller*> controller);

	void setRoundRadius(int radius);
	void refreshStickers();
	void showMegagroupSet(ChannelData *megagroup);
	void setCurrentPeer(PeerData *peer);

	void hideFinished();
	void showStarted();
	void beforeHiding();
	void afterShown();

	int marginTop() const;
	int marginBottom() const;

	bool preventAutoHide() const;
	bool isSliding() const {
		return _a_slide.animating();
	}

	void setAfterShownCallback(Fn<void(SelectorTab)> callback) {
		_afterShownCallback = std::move(callback);
	}
	void setBeforeHidingCallback(Fn<void(SelectorTab)> callback) {
		_beforeHidingCallback = std::move(callback);
	}

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e);
	QRect rectForFloatPlayer() const;

	auto showRequests() const {
		return _showRequests.events();
	}

	~TabbedSelector();

	class Inner;
	class InnerFooter;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onScroll();

signals:
	void emojiSelected(EmojiPtr emoji);
	void stickerOrGifSelected(not_null<DocumentData*> sticker);
	void photoSelected(not_null<PhotoData*> photo);
	void inlineResultSelected(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot);

	void cancelled();
	void slideFinished();
	void checkForHide();

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
		not_null<Inner*> widget() const {
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

	void paintSlideFrame(Painter &p, TimeMs ms);
	void paintContent(Painter &p);

	void checkRestrictedPeer();
	bool isRestrictedView();
	void updateRestrictedLabelGeometry();

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

	int _roundRadius = 0;
	int _footerTop = 0;
	PeerData *_currentPeer = nullptr;

	class SlideAnimation;
	std::unique_ptr<SlideAnimation> _slideAnimation;
	Animation _a_slide;

	object_ptr<Ui::SettingsSlider> _tabsSlider = { nullptr };
	object_ptr<Ui::PlainShadow> _topShadow;
	object_ptr<Ui::PlainShadow> _bottomShadow;
	object_ptr<Ui::ScrollArea> _scroll;
	object_ptr<Ui::FlatLabel> _restrictedLabel = { nullptr };
	std::array<Tab, Tab::kCount> _tabs;
	SelectorTab _currentTabType = SelectorTab::Emoji;

	Fn<void(SelectorTab)> _afterShownCallback;
	Fn<void(SelectorTab)> _beforeHidingCallback;

	rpl::event_stream<> _showRequests;

};

class TabbedSelector::Inner : public Ui::RpWidget {
	Q_OBJECT

public:
	Inner(QWidget *parent, not_null<Window::Controller*> controller);

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

	virtual object_ptr<InnerFooter> createFooter() = 0;

signals:
	void scrollToY(int y);
	void disableScroll(bool disabled);

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	int minimalHeight() const;
	int resizeGetHeight(int newWidth) override final;

	not_null<Window::Controller*> controller() const {
		return _controller;
	}

	virtual int countDesiredHeight(int newWidth) = 0;
	virtual InnerFooter *getFooter() const = 0;
	virtual void processHideFinished() {
	}
	virtual void processPanelHideFinished() {
	}

private:
	not_null<Window::Controller*> _controller;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _minimalHeight = 0;

};

class TabbedSelector::InnerFooter : public TWidget {
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
