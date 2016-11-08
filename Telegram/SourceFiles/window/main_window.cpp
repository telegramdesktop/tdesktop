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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "window/main_window.h"

#include "localstorage.h"
#include "styles/style_window.h"
#include "platform/platform_window_title.h"
#include "window/window_theme.h"

namespace Window {

MainWindow::MainWindow() : QWidget()
, _positionUpdatedTimer(this)
, _body(this) {
	subscribe(Theme::Background(), [this](const Theme::BackgroundUpdate &data) {
		using Type = Theme::BackgroundUpdate::Type;
		if (data.type == Type::TestingTheme || data.type == Type::RevertingTheme || data.type == Type::ApplyingTheme) {
			if (_title) {
				_title->update();
			}
		}
	});
}

void MainWindow::init() {
	initHook();

	_positionUpdatedTimer->setSingleShot(true);
	connect(_positionUpdatedTimer, SIGNAL(timeout()), this, SLOT(savePositionByTimer()));

	auto p = palette();
	p.setColor(QPalette::Window, st::windowBg->c);
	setPalette(p);

	if ((_title = Platform::CreateTitleWidget(this))) {
		_title->init();
	}

	initSize();
}

HitTestResult MainWindow::hitTest(const QPoint &p) const {
	auto titleResult = _title ? _title->hitTest(p - _title->geometry().topLeft()) : Window::HitTestResult::None;
	if (titleResult != Window::HitTestResult::None) {
		return titleResult;
	} else if (rect().contains(p)) {
		return Window::HitTestResult::Client;
	}
	return Window::HitTestResult::None;
}

void MainWindow::initSize() {
	setMinimumWidth(st::windowMinWidth);
	setMinimumHeight((_title ? _title->height() : 0) + st::windowMinHeight);

	auto pos = cWindowPos();
	auto avail = QDesktopWidget().availableGeometry();
	bool maximized = false;
	auto geom = QRect(avail.x() + (avail.width() - st::windowDefaultWidth) / 2, avail.y() + (avail.height() - st::windowDefaultHeight) / 2, st::windowDefaultWidth, st::windowDefaultHeight);
	if (pos.w && pos.h) {
		for (auto screen : QGuiApplication::screens()) {
			if (pos.moncrc == screenNameChecksum(screen->name())) {
				auto screenGeometry = screen->geometry();
				auto w = screenGeometry.width(), h = screenGeometry.height();
				if (w >= st::windowMinWidth && h >= st::windowMinHeight) {
					if (pos.w > w) pos.w = w;
					if (pos.h > h) pos.h = h;
					pos.x += screenGeometry.x();
					pos.y += screenGeometry.y();
					if (pos.x + st::windowMinWidth <= screenGeometry.x() + screenGeometry.width() &&
						pos.y + st::windowMinHeight <= screenGeometry.y() + screenGeometry.height()) {
						geom = QRect(pos.x, pos.y, pos.w, pos.h);
					}
				}
				break;
			}
		}

		if (pos.y < 0) pos.y = 0;
		maximized = pos.maximized;
	}
	setGeometry(geom);
}

void MainWindow::positionUpdated() {
	_positionUpdatedTimer->start(SaveWindowPositionTimeout);
}

void MainWindow::setTitleVisibility(bool visible) {
	if (_title && (_title->isHidden() == visible)) {
		_title->setVisible(visible);
		updateControlsGeometry();
	}
}

int32 MainWindow::screenNameChecksum(const QString &name) const {
	auto bytes = name.toUtf8();
	return hashCrc32(bytes.constData(), bytes.size());
}

void MainWindow::setPositionInited() {
	_positionInited = true;
}

void MainWindow::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void MainWindow::updateControlsGeometry() {
	auto bodyTop = 0;
	if (_title && !_title->isHidden()) {
		_title->setGeometry(0, bodyTop, width(), _title->height());
		bodyTop += _title->height();
	}
	_body->setGeometry(0, bodyTop, width(), height() - bodyTop);
}

void MainWindow::savePosition(Qt::WindowState state) {
	if (state == Qt::WindowActive) state = windowHandle()->windowState();
	if (state == Qt::WindowMinimized || !positionInited()) return;

	auto pos = cWindowPos(), curPos = pos;

	if (state == Qt::WindowMaximized) {
		curPos.maximized = 1;
	} else {
		auto r = geometry();
		curPos.x = r.x();
		curPos.y = r.y();
		curPos.w = r.width();
		curPos.h = r.height();
		curPos.maximized = 0;
	}

	int px = curPos.x + curPos.w / 2, py = curPos.y + curPos.h / 2;
	int minDelta = 0;
	QScreen *chosen = 0;
	auto screens = QGuiApplication::screens();
	for (auto screen : QGuiApplication::screens()) {
		auto delta = (screen->geometry().center() - QPoint(px, py)).manhattanLength();
		if (!chosen || delta < minDelta) {
			minDelta = delta;
			chosen = screen;
		}
	}
	if (chosen) {
		curPos.x -= chosen->geometry().x();
		curPos.y -= chosen->geometry().y();
		curPos.moncrc = screenNameChecksum(chosen->name());
	}

	if (curPos.w >= st::windowMinWidth && curPos.h >= st::windowMinHeight) {
		if (curPos.x != pos.x || curPos.y != pos.y || curPos.w != pos.w || curPos.h != pos.h || curPos.moncrc != pos.moncrc || curPos.maximized != pos.maximized) {
			cSetWindowPos(curPos);
			Local::writeSettings();
		}
	}
}

MainWindow::~MainWindow() {
}

void MainWindow::closeWithoutDestroy() {
	hide();
}

} // namespace Window
