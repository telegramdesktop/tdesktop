/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "base/timer.h"
#include "base/object_ptr.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class PanelAnimation;
} // namespace Ui

namespace ChatHelpers {

class TabbedSelector;

extern const char kOptionTabbedPanelShowOnClick[];
[[nodiscard]] bool ShowPanelOnClick();

struct TabbedPanelDescriptor {
	Window::SessionController *regularWindow = nullptr;
	object_ptr<TabbedSelector> ownedSelector = { nullptr };
	TabbedSelector *nonOwnedSelector = nullptr;
};

class TabbedPanel : public Ui::RpWidget {
public:
	TabbedPanel(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<TabbedSelector*> selector);
	TabbedPanel(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		object_ptr<TabbedSelector> selector);
	TabbedPanel(QWidget *parent, TabbedPanelDescriptor &&descriptor);

	[[nodiscard]] bool isSelectorStolen() const;
	[[nodiscard]] not_null<TabbedSelector*> selector() const;
	[[nodiscard]] rpl::producer<bool> pauseAnimations() const;

	void moveBottomRight(int bottom, int right);
	void moveTopRight(int top, int right);
	void setDesiredHeightValues(
		float64 ratio,
		int minHeight,
		int maxHeight);
	void setDropDown(bool dropDown);

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
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void otherEnter();
	void otherLeave();

	void paintEvent(QPaintEvent *e) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

private:
	void hideByTimerOrLeave();
	void moveHorizontally();
	void showFromSelector();

	style::margins innerPadding() const;

	// Rounded rect which has shadow around it.
	QRect innerRect() const;

	QImage grabForAnimation();
	void startShowAnimation();
	void startOpacityAnimation(bool hiding);
	void prepareCacheFor(bool hiding);

	void opacityAnimationCallback();

	void hideFinished();
	void showStarted();

	bool preventAutoHide() const;
	void updateContentHeight();

	Window::SessionController * const _regularWindow = nullptr;
	const object_ptr<TabbedSelector> _ownedSelector = { nullptr };
	const not_null<TabbedSelector*> _selector;
	rpl::event_stream<bool> _pauseAnimations;

	int _contentMaxHeight = 0;
	int _contentHeight = 0;
	int _top = 0;
	int _bottom = 0;
	int _right = 0;
	float64 _heightRatio = 1.;
	int _minContentHeight = 0;
	int _maxContentHeight = 0;

	std::unique_ptr<Ui::PanelAnimation> _showAnimation;
	Ui::Animations::Simple _a_show;

	bool _shouldFinishHide = false;
	bool _dropDown = false;

	bool _hiding = false;
	bool _hideAfterSlide = false;
	QPixmap _cache;
	Ui::Animations::Simple _a_opacity;
	base::Timer _hideTimer;

};

} // namespace ChatHelpers
