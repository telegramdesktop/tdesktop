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
#include "settings/settings_layer.h"

#include "settings/settings_inner_widget.h"
#include "settings/settings_fixed_bar.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "ui/effects/widget_fade_wrap.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "storage/localstorage.h"
#include "boxes/confirm_box.h"
#include "application.h"
#include "core/file_utilities.h"
#include "window/themes/window_theme.h"

namespace Settings {

Layer::Layer()
: _scroll(this, st::settingsScroll)
, _fixedBar(this)
, _fixedBarClose(this, st::settingsFixedBarClose)
, _fixedBarShadow(this, object_ptr<BoxLayerTitleShadow>(this)) {
	_fixedBar->moveToLeft(0, st::boxRadius);
	_fixedBarClose->moveToRight(0, 0);
	_fixedBarShadow->entity()->resize(width(), st::lineWidth);
	_fixedBarShadow->moveToLeft(0, _fixedBar->y() + _fixedBar->height());
	_fixedBarShadow->hideFast();
	_scroll->moveToLeft(0, st::settingsFixedBarHeight);

	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
}

void Layer::setCloseClickHandler(base::lambda<void()> callback) {
	_fixedBarClose->setClickedCallback(std::move(callback));
}

void Layer::onScroll() {
	if (_scroll->scrollTop() > 0) {
		_fixedBarShadow->showAnimated();
	} else {
		_fixedBarShadow->hideAnimated();
	}
}

void Layer::resizeToWidth(int newWidth, int newContentLeft) {
	// Widget height depends on InnerWidget height, so we
	// resize it here, not in the resizeEvent() handler.
	_inner->resizeToWidth(newWidth, newContentLeft);

	resizeUsingInnerHeight(newWidth, _inner->height());
}

void Layer::onInnerHeightUpdated() {
	resizeUsingInnerHeight(width(), _inner->height());
}

void Layer::doSetInnerWidget(object_ptr<LayerInner> widget) {
	_inner = _scroll->setOwnedWidget(std::move(widget));
	connect(_inner, SIGNAL(heightUpdated()), this, SLOT(onInnerHeightUpdated()));
}

void Layer::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	if (_roundedCorners) {
		auto paintTopRounded = clip.intersects(QRect(0, 0, width(), st::boxRadius));
		auto paintBottomRounded = clip.intersects(QRect(0, height() - st::boxRadius, width(), st::boxRadius));
		if (paintTopRounded || paintBottomRounded) {
			auto parts = RectPart::None | 0;
			if (paintTopRounded) parts |= RectPart::FullTop;
			if (paintBottomRounded) parts |= RectPart::FullBottom;
			App::roundRect(p, rect(), st::boxBg, BoxCorners, nullptr, parts);
		}
		auto other = clip.intersected(QRect(0, st::boxRadius, width(), height() - 2 * st::boxRadius));
		if (!other.isEmpty()) {
			p.fillRect(other, st::boxBg);
		}
	} else {
		p.fillRect(e->rect(), st::boxBg);
	}
}

void Layer::resizeEvent(QResizeEvent *e) {
	LayerWidget::resizeEvent(e);
	if (!width() || !height()) {
		return;
	}

	_fixedBar->resizeToWidth(width());
	_fixedBar->moveToLeft(0, st::boxRadius);
	_fixedBarClose->moveToRight(0, 0);
	auto shadowTop = _fixedBar->y() + _fixedBar->height();
	_fixedBarShadow->entity()->resize(width(), st::lineWidth);
	_fixedBarShadow->moveToLeft(0, shadowTop);

	auto scrollSize = QSize(width(), height() - shadowTop - (_roundedCorners ? st::boxRadius : 0));
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
	}
	if (!_scroll->isHidden()) {
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	}
}

void Layer::setTitle(const QString &title) {
	_fixedBar->setText(title);
}

} // namespace Settings
