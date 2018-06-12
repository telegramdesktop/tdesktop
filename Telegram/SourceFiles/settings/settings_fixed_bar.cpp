/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_fixed_bar.h"

#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "mainwindow.h"

namespace Settings {

FixedBar::FixedBar(QWidget *parent) : TWidget(parent) {
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void FixedBar::setText(const QString &text) {
	_text = text;
	update();
}

int FixedBar::resizeGetHeight(int newWidth) {
	return st::settingsFixedBarHeight - st::boxRadius;
}

void FixedBar::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::boxBg);

	p.setFont(st::settingsFixedBarFont);
	p.setPen(st::windowFg);
	p.drawTextLeft(st::settingsFixedBarTextPosition.x(), st::settingsFixedBarTextPosition.y() - st::boxRadius, width(), _text);
}

} // namespace Settings
