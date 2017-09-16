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

#include "ui/rp_widget.h"
#include "base/timer.h"

namespace Window {
class Controller;
} // namespace Window

namespace Ui {
class PanelAnimation;
} // namespace Ui

namespace ChatHelpers {

class TabbedSelector;

class TabbedPanel : public Ui::RpWidget{
	Q_OBJECT

public:
	TabbedPanel(QWidget *parent, not_null<Window::Controller*> controller);
	TabbedPanel(QWidget *parent, not_null<Window::Controller*> controller, object_ptr<TabbedSelector> selector);

	object_ptr<TabbedSelector> takeSelector();
	QPointer<TabbedSelector> getSelector() const;
	void moveBottom(int bottom);

	void hideFast();
	bool hiding() const {
		return _hiding || _hideTimer.isActive();
	}

	bool overlaps(const QRect &globalRect) const;

	void showAnimated();
	void hideAnimated();
	void toggleAnimated();

	~TabbedPanel();

protected:
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void otherEnter();
	void otherLeave();

	void paintEvent(QPaintEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

private slots:
	void onWndActiveChanged();

private:
	void hideByTimerOrLeave();
	void moveByBottom();
	bool isDestroying() const {
		return !_selector;
	}
	void showFromSelector();

	style::margins innerPadding() const;

	// Rounded rect which has shadow around it.
	QRect innerRect() const;

	// Inner rect with removed st::buttonRadius from top and bottom.
	// This one is allowed to be not rounded.
	QRect horizontalRect() const;

	// Inner rect with removed st::buttonRadius from left and right.
	// This one is allowed to be not rounded.
	QRect verticalRect() const;

	QImage grabForAnimation();
	void startShowAnimation();
	void startOpacityAnimation(bool hiding);
	void prepareCache();

	void opacityAnimationCallback();

	void hideFinished();
	void showStarted();

	bool preventAutoHide() const;
	void updateContentHeight();

	not_null<Window::Controller*> _controller;
	object_ptr<TabbedSelector> _selector;

	int _contentMaxHeight = 0;
	int _contentHeight = 0;
	int _bottom = 0;

	std::unique_ptr<Ui::PanelAnimation> _showAnimation;
	Animation _a_show;

	bool _hiding = false;
	bool _hideAfterSlide = false;
	QPixmap _cache;
	Animation _a_opacity;
	base::Timer _hideTimer;

};

} // namespace ChatHelpers
