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
#include "boxes/notifications_box.h"

#include "lang.h"
#include "ui/buttons/round_button.h"
#include "ui/widgets/discrete_slider.h"
#include "styles/style_boxes.h"

namespace {

constexpr int kMaxNotificationsCount = 5;

} // namespace

NotificationsBox::NotificationsBox() : AbstractBox()
, _countSlider(this)
, _save(this, lang(lng_settings_save), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton) {
	for (int i = 0; i != kMaxNotificationsCount; ++i) {
		_countSlider->addSection(QString::number(i + 1));
	}
	_countSlider->setActiveSectionFast(2);

	setMouseTracking(true);
	_save->setClickedCallback([this] {

	});
	connect(_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	setMaxHeight(st::notificationsBoxHeight);

	prepare();
}

void NotificationsBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	auto contentLeft = getContentLeft();

	p.setFont(st::boxTitleFont);
	p.setPen(st::boxTitleFg);
	p.drawTextLeft(contentLeft, st::boxTitlePosition.y(), width(), lang(lng_settings_notifications_position));

	auto screenLeft = (width() - st::notificationsBoxScreenSize.width()) / 2;
	auto screenRect = getScreenRect();
	p.fillRect(screenRect.x(), screenRect.y(), st::notificationsBoxScreenSize.width(), st::notificationsBoxScreenSize.height(), st::notificationsBoxScreenBg);

	auto monitorTop = st::notificationsBoxMonitorTop;
	st::notificationsBoxMonitor.paint(p, contentLeft, monitorTop, width());

	auto labelTop = screenRect.y() + screenRect.height() + st::notificationsBoxCountLabelTop;
	p.drawTextLeft(contentLeft, labelTop, width(), lang(lng_settings_notifications_count));
}

int NotificationsBox::getContentLeft() const {
	return (width() - st::notificationsBoxMonitor.width()) / 2;
}

QRect NotificationsBox::getScreenRect() const {
	auto screenLeft = (width() - st::notificationsBoxScreenSize.width()) / 2;
	auto screenTop = st::notificationsBoxMonitorTop + st::notificationsBoxScreenTop;
	return QRect(screenLeft, screenTop, st::notificationsBoxScreenSize.width(), st::notificationsBoxScreenSize.height());
}

void NotificationsBox::resizeEvent(QResizeEvent *e) {
	_save->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save->height());
	_cancel->moveToRight(st::boxButtonPadding.right() + _save->width() + st::boxButtonPadding.left(), _save->y());

	auto screenRect = getScreenRect();
	auto sliderTop = screenRect.y() + screenRect.height() + st::notificationsBoxCountLabelTop + st::notificationsBoxCountTop;
	auto contentLeft = getContentLeft();
	_countSlider->resizeToWidth(width() - 2 * contentLeft);
	_countSlider->move(contentLeft, sliderTop);
	AbstractBox::resizeEvent(e);
}

void NotificationsBox::mousePressEvent(QMouseEvent *e) {

}

void NotificationsBox::mouseMoveEvent(QMouseEvent *e) {

}

void NotificationsBox::mouseReleaseEvent(QMouseEvent *e) {

}
