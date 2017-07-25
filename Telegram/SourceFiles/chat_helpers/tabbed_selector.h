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

class TabbedSelector : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	TabbedSelector(QWidget *parent, gsl::not_null<Window::Controller*> controller);

	void setRoundRadius(int radius);
	void refreshStickers();
	void stickersInstalled(uint64 setId);
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

	void setAfterShownCallback(base::lambda<void(SelectorTab)> callback) {
		_afterShownCallback = std::move(callback);
	}
	void setBeforeHidingCallback(base::lambda<void(SelectorTab)> callback) {
		_beforeHidingCallback = std::move(callback);
	}

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e);
	QRect rectForFloatPlayer();

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
	void stickerSelected(DocumentData *sticker);
	void photoSelected(PhotoData *photo);
	void inlineResultSelected(InlineBots::Result *result, UserData *bot);

	void updateStickers();

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
		gsl::not_null<Inner*> widget() const {
			return _weak;
		}
		gsl::not_null<InnerFooter*> footer() const {
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

	QImage grabForAnimation();

	void scrollToY(int y);

	void showAll();
	void hideForSliding();

	bool hasSectionIcons() const;
	void setWidgetToScrollArea();
	void createTabsSlider();
	void switchTab();
	gsl::not_null<Tab*> getTab(SelectorTab type) {
		return &_tabs[static_cast<int>(type)];
	}
	gsl::not_null<const Tab*> getTab(SelectorTab type) const {
		return &_tabs[static_cast<int>(type)];
	}
	gsl::not_null<Tab*> currentTab() {
		return getTab(_currentTabType);
	}
	gsl::not_null<const Tab*> currentTab() const {
		return getTab(_currentTabType);
	}
	gsl::not_null<EmojiListWidget*> emoji() const;
	gsl::not_null<StickersListWidget*> stickers() const;
	gsl::not_null<GifsListWidget*> gifs() const;

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

	base::lambda<void(SelectorTab)> _afterShownCallback;
	base::lambda<void(SelectorTab)> _beforeHidingCallback;

};

class TabbedSelector::Inner : public TWidget {
	Q_OBJECT

public:
	Inner(QWidget *parent, gsl::not_null<Window::Controller*> controller);

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
	gsl::not_null<Window::Controller*> controller() const {
		return _controller;
	}

	virtual int countHeight() = 0;
	virtual InnerFooter *getFooter() const = 0;
	virtual void processHideFinished() {
	}
	virtual void processPanelHideFinished() {
	}

private:
	gsl::not_null<Window::Controller*> _controller;

	int _visibleTop = 0;
	int _visibleBottom = 0;

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
