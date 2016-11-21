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
#include "platform/mac/window_title_mac.h"

#include "ui/widgets/shadow.h"
#include "styles/style_window.h"
#include "platform/platform_main_window.h"

namespace Platform {

TitleWidget::TitleWidget(MainWindow *parent, int height) : Window::TitleWidget(parent)
, _shadow(this, st::titleShadow) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	resize(width(), height);

#ifndef OS_MAC_OLD
	QStringList families = { qsl(".SF NS Text"), qsl("Helvetica Neue") };
	for (auto family : families) {
		_font.setFamily(family);
		if (QFontInfo(_font).family() == _font.family()) {
			break;
		}
	}
#endif // OS_MAC_OLD

	if (QFontInfo(_font).family() == _font.family()) {
		_font.setPixelSize((height * 15) / 24);
	} else {
		_font = st::normalFont;
	}

	subscribe(Global::RefUnreadCounterUpdate(), [this] { update(); });
}

void TitleWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(rect(), st::titleBg);

	p.setPen(isActiveWindow() ? st::titleFgActive : st::titleFg);
	p.setFont(_font);

	p.drawText(rect(), static_cast<MainWindow*>(parentWidget())->titleText(), style::al_center);
}

void TitleWidget::resizeEvent(QResizeEvent *e) {
	_shadow->setGeometry(0, height() - st::lineWidth, width(), st::lineWidth);
}

void TitleWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	auto window = parentWidget();
	if (window->windowState() == Qt::WindowMaximized) {
		window->setWindowState(Qt::WindowNoState);
	} else {
		window->setWindowState(Qt::WindowMaximized);
	}
}

Window::TitleWidget *CreateTitleWidget(QWidget *parent) {
	if (auto window = qobject_cast<Platform::MainWindow*>(parent)) {
		if (auto height = window->getCustomTitleHeight()) {
			return new TitleWidget(window, height);
		}
	}
	return nullptr;
}

} // namespace Platform
