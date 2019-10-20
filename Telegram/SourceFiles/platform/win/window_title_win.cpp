/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/window_title_win.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "styles/style_window.h"

#include <QtGui/QWindow>

namespace Platform {

TitleWidget::TitleWidget(QWidget *parent)
: Window::TitleWidget(parent)
, _st(st::defaultWindowTitle)
, _minimize(this, _st.minimize)
, _maximizeRestore(this, _st.maximize)
, _close(this, _st.close)
, _shadow(this, st::titleShadow)
, _maximizedState(parent->window()->windowState() & Qt::WindowMaximized) {
	_minimize->setClickedCallback([this]() {
		window()->setWindowState(Qt::WindowMinimized);
		_minimize->clearState();
	});
	_minimize->setPointerCursor(false);
	_maximizeRestore->setClickedCallback([this]() {
		window()->setWindowState(_maximizedState ? Qt::WindowNoState : Qt::WindowMaximized);
		_maximizeRestore->clearState();
	});
	_maximizeRestore->setPointerCursor(false);
	_close->setClickedCallback([this]() {
		window()->close();
		_close->clearState();
	});
	_close->setPointerCursor(false);

	setAttribute(Qt::WA_OpaquePaintEvent);
	resize(width(), _st.height);
}

void TitleWidget::init() {
	connect(window()->windowHandle(), SIGNAL(windowStateChanged(Qt::WindowState)), this, SLOT(onWindowStateChanged(Qt::WindowState)));
	_maximizedState = (window()->windowState() & Qt::WindowMaximized);
	_activeState = isActiveWindow();
	updateButtonsState();
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	auto active = isActiveWindow();
	if (_activeState != active) {
		_activeState = active;
		updateButtonsState();
	}
	Painter(this).fillRect(rect(), active ? _st.bgActive : _st.bg);
}

void TitleWidget::updateControlsPosition() {
	auto right = 0;
	_close->moveToRight(right, 0); right += _close->width();
	_maximizeRestore->moveToRight(right, 0); right += _maximizeRestore->width();
	_minimize->moveToRight(right, 0);
}

void TitleWidget::resizeEvent(QResizeEvent *e) {
	updateControlsPosition();
	_shadow->setGeometry(0, height() - st::lineWidth, width(), st::lineWidth);
}

void TitleWidget::updateControlsVisibility() {
	updateControlsPosition();
	update();
}

void TitleWidget::onWindowStateChanged(Qt::WindowState state) {
	if (state == Qt::WindowMinimized) return;

	auto maximized = (state == Qt::WindowMaximized);
	if (_maximizedState != maximized) {
		_maximizedState = maximized;
		updateButtonsState();
	}
}

void TitleWidget::updateButtonsState() {
	_minimize->setIconOverride(_activeState
		? &_st.minimizeIconActive
		: nullptr,
		_activeState
		? &_st.minimizeIconActiveOver
		: nullptr);
	if (_maximizedState) {
		_maximizeRestore->setIconOverride(
			_activeState
			? &_st.restoreIconActive : &_st.restoreIcon,
			_activeState
			? &_st.restoreIconActiveOver
			: &_st.restoreIconOver);
	} else {
		_maximizeRestore->setIconOverride(_activeState
			? &_st.maximizeIconActive
			: nullptr,
			_activeState
			? &_st.maximizeIconActiveOver
			: nullptr);
	}
	_close->setIconOverride(_activeState
		? &_st.closeIconActive
		: nullptr,
		_activeState
		? &_st.closeIconActiveOver
		: nullptr);
}

Window::HitTestResult TitleWidget::hitTest(const QPoint &p) const {
	if (false
		|| (_minimize->geometry().contains(p))
		|| (_maximizeRestore->geometry().contains(p))
		|| (_close->geometry().contains(p))
	) {
		return Window::HitTestResult::SysButton;
	} else if (rect().contains(p)) {
		return Window::HitTestResult::Caption;
	}
	return Window::HitTestResult::None;
}

} // namespace Platform
