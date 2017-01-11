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
#include "stdafx.h"
#include "platform/win/window_title_win.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "styles/style_window.h"

namespace Platform {

TitleWidget::TitleWidget(QWidget *parent) : Window::TitleWidget(parent)
, _minimize(this, st::titleButtonMinimize)
, _maximizeRestore(this, st::titleButtonMaximize)
, _close(this, st::titleButtonClose)
, _shadow(this, st::titleShadow)
, _maximized(parent->window()->windowState() & Qt::WindowMaximized) {
	_minimize->setClickedCallback([this]() {
		window()->setWindowState(Qt::WindowMinimized);
		_minimize->clearState();
	});
	_minimize->setPointerCursor(false);
	_maximizeRestore->setClickedCallback([this]() {
		window()->setWindowState(_maximized ? Qt::WindowNoState : Qt::WindowMaximized);
		_maximizeRestore->clearState();
	});
	_maximizeRestore->setPointerCursor(false);
	_close->setClickedCallback([this]() {
		window()->close();
		_close->clearState();
	});
	_close->setPointerCursor(false);

	setAttribute(Qt::WA_OpaquePaintEvent);
	resize(width(), st::titleHeight);
}

void TitleWidget::init() {
	connect(window()->windowHandle(), SIGNAL(windowStateChanged(Qt::WindowState)), this, SLOT(onWindowStateChanged(Qt::WindowState)));
	_maximized = (window()->windowState() & Qt::WindowMaximized);
	updateMaximizeRestoreButton();
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	Painter(this).fillRect(rect(), st::titleBg);
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
	if (_maximized != maximized) {
		_maximized = maximized;
		updateMaximizeRestoreButton();
	}
}

void TitleWidget::updateMaximizeRestoreButton() {
	if (_maximized) {
		_maximizeRestore->setIconOverride(&st::titleButtonRestoreIcon, &st::titleButtonRestoreIconOver);
	} else {
		_maximizeRestore->setIconOverride(nullptr, nullptr);
	}
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
