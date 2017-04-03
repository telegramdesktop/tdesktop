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
class RippleAnimation;
class SettingsSlider;
} // namesapce Ui

namespace ChatHelpers {

class EmojiListWidget;
class StickersListWidget;
class GifsListWidget;

class EmojiPanel : public TWidget {
	Q_OBJECT

public:
	EmojiPanel(QWidget *parent);

	void moveBottom(int bottom);

	void hideFast();
	bool hiding() const {
		return _hiding || _hideTimer.isActive();
	}

	void stickersInstalled(uint64 setId);

	bool overlaps(const QRect &globalRect) const;
	void setInlineQueryPeer(PeerData *peer);

	void showAnimated();
	void hideAnimated();

	~EmojiPanel();

	class Inner;
	class InnerFooter;

protected:
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void otherEnter();
	void otherLeave();

	void paintEvent(QPaintEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

public slots:
	void refreshStickers();

private slots:
	void hideByTimerOrLeave();

	void onWndActiveChanged();
	void onScroll();

	void onCheckForHide();

	void onSaveConfig();
	void onSaveConfigDelayed(int delay);

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

		Tab(TabType type, object_ptr<Inner> widget);

		object_ptr<Inner> takeWidget();
		void returnWidget(object_ptr<Inner> widget);

		TabType type() const {
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
		TabType _type = TabType::Emoji;
		object_ptr<Inner> _widget = { nullptr };
		QPointer<Inner> _weak;
		object_ptr<InnerFooter> _footer;
		int _scrollTop = 0;

	};

	int marginTop() const;
	int marginBottom() const;
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

	void scrollToY(int y);

	void opacityAnimationCallback();

	void hideFinished();
	void showStarted();

	bool preventAutoHide() const;
	void updateContentHeight();

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
	gsl::not_null<EmojiListWidget*> emoji() const;
	gsl::not_null<StickersListWidget*> stickers() const;
	gsl::not_null<GifsListWidget*> gifs() const;

	int _contentMaxHeight = 0;
	int _contentHeight = 0;

	int _width = 0;
	int _height = 0;
	int _bottom = 0;
	int _footerTop = 0;

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

	object_ptr<Ui::SettingsSlider> _tabsSlider = { nullptr };
	object_ptr<Ui::PlainShadow> _topShadow;
	object_ptr<Ui::PlainShadow> _bottomShadow;
	object_ptr<Ui::ScrollArea> _scroll;
	std::array<Tab, Tab::kCount> _tabs;
	TabType _currentTabType = TabType::Emoji;

	QTimer _saveConfigTimer;

};

class EmojiPanel::Inner : public TWidget {
	Q_OBJECT

public:
	Inner(QWidget *parent);

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
	void saveConfigDelayed(int delay);

protected:
	virtual int countHeight() = 0;
	virtual InnerFooter *getFooter() const = 0;
	virtual void processHideFinished() {
	}
	virtual void processPanelHideFinished() {
	}

private:
	int _visibleTop = 0;
	int _visibleBottom = 0;

};

class EmojiPanel::InnerFooter : public TWidget {
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
