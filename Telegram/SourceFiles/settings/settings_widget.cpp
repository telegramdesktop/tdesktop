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
#include "settings/settings_widget.h"

#include "settings/settings_inner_widget.h"
#include "settings/settings_fixed_bar.h"
#include "styles/style_settings.h"
#include "ui/scrollarea.h"
#include "mainwindow.h"

namespace Settings {

Widget::Widget() : LayerWidget()
, _scroll(this, st::setScroll)
, _inner(this)
, _fixedBar(this)
, _fixedBarShadow1(this, st::settingsFixedBarShadowBg1)
, _fixedBarShadow2(this, st::settingsFixedBarShadowBg2) {
	_scroll->setOwnedWidget(_inner);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_fixedBar->move(0, 0);
	_fixedBarShadow1->move(0, _fixedBar->y() + st::settingsFixedBarHeight);
	_fixedBarShadow2->move(0, _fixedBarShadow1->y() + st::lineWidth);
	_scroll->move(0, st::settingsFixedBarHeight);

	connect(_inner, SIGNAL(heightUpdated()), this, SLOT(onInnerHeightUpdated()));
}

void Widget::parentResized() {
	int windowWidth = App::wnd()->width();
	int newWidth = st::settingsMaxWidth;
	int newContentLeft = st::settingsMaxPadding;
	if (windowWidth <= st::settingsMaxWidth) {
		newWidth = windowWidth;
		newContentLeft = st::settingsMinPadding;
		if (windowWidth > st::wndMinWidth) {
			// Width changes from st::wndMinWidth to st::settingsMaxWidth.
			// Padding changes from st::settingsMinPadding to st::settingsMaxPadding.
			newContentLeft += ((newWidth - st::wndMinWidth) * (st::settingsMaxPadding - st::settingsMinPadding)) / (st::settingsMaxWidth - st::wndMinWidth);
		}
	} else if (windowWidth < st::settingsMaxWidth + 2 * st::settingsMargin) {
		newWidth = windowWidth - 2 * st::settingsMargin;
		newContentLeft = st::settingsMinPadding;
		if (windowWidth > st::wndMinWidth) {
			// Width changes from st::wndMinWidth to st::settingsMaxWidth.
			// Padding changes from st::settingsMinPadding to st::settingsMaxPadding.
			newContentLeft += ((newWidth - st::wndMinWidth) * (st::settingsMaxPadding - st::settingsMinPadding)) / (st::settingsMaxWidth - st::wndMinWidth);
		}
	}

	// Widget height depends on InnerWidget height, so we
	// resize it here, not in the resizeEvent() handler.
	_inner->resizeToWidth(newWidth, newContentLeft);

	resizeUsingInnerHeight(newWidth, newContentLeft);
}

void Widget::onInnerHeightUpdated() {
	resizeUsingInnerHeight(width(), _contentLeft);
}

void Widget::resizeUsingInnerHeight(int newWidth, int newContentLeft) {
	if (!App::wnd()) return;

	int windowWidth = App::wnd()->width();
	int windowHeight = App::wnd()->height();
	int maxHeight = st::settingsFixedBarHeight + _inner->height();
	int newHeight = maxHeight;
	if (newHeight > windowHeight || newWidth >= windowWidth) {
		newHeight = windowHeight;
	}

	if (_contentLeft != newContentLeft) {
		_contentLeft = newContentLeft;
	}

	setGeometry((App::wnd()->width() - newWidth) / 2, (App::wnd()->height() - newHeight) / 2, newWidth, newHeight);
	update();
}

void Widget::showDone() {
	_inner->showFinished();
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(rect(), st::windowBg);
}

void Widget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}

	_fixedBar->resizeToWidth(width());
	_fixedBarShadow1->resize(width(), st::lineWidth);
	_fixedBarShadow2->resize(width(), st::lineWidth);

	QSize scrollSize(width(), height() - _fixedBar->height());
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
	}

	if (!_scroll->isHidden()) {
		int scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	}
}

} // namespace Settings
