/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

class TabbedPanel : public Ui::RpWidget {
public:
	TabbedPanel(QWidget *parent, not_null<Window::Controller*> controller);
	TabbedPanel(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		object_ptr<TabbedSelector> selector);

	object_ptr<TabbedSelector> takeSelector();
	QPointer<TabbedSelector> getSelector() const;
	void moveBottomRight(int bottom, int right);
	void setDesiredHeightValues(
		float64 ratio,
		int minHeight,
		int maxHeight);

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

private:
	void hideByTimerOrLeave();
	void moveByBottom();
	bool isDestroying() const {
		return !_selector;
	}
	void showFromSelector();
	void windowActiveChanged();

	style::margins innerPadding() const;

	// Rounded rect which has shadow around it.
	QRect innerRect() const;

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
	int _right = 0;
	float64 _heightRatio = 1.;
	int _minContentHeight = 0;
	int _maxContentHeight = 0;

	std::unique_ptr<Ui::PanelAnimation> _showAnimation;
	Animation _a_show;

	bool _hiding = false;
	bool _hideAfterSlide = false;
	QPixmap _cache;
	Animation _a_opacity;
	base::Timer _hideTimer;

};

} // namespace ChatHelpers
