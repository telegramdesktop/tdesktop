/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "profile/profile_back_button.h"

#include "ui/painter.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_profile.h"
#include "styles/style_info.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

#include <QGraphicsOpacityEffect>

namespace Profile {

BackButton::BackButton(QWidget *parent) : Ui::AbstractButton(parent) {
	setCursor(style::cur_pointer);
}

void BackButton::setText(const QString &text) {
	_text = text;
	_cachedWidth = -1;
	update();
}

void BackButton::setSubtext(const QString &subtext) {
	_subtext = subtext;
	_cachedWidth = -1;
	update();
}

void BackButton::setWidget(not_null<Ui::RpWidget*> widget) {
	_widget = widget;
	_widget->setParent(this);
	_widget->show();
	_widget->move(
		st::historyAdminLogTopBarLeft + st::historyAdminLogTopBarUserpicSkip,
		(st::profileTopBarHeight - _widget->height()) / 2);
	_cachedWidth = -1;
	update();
}

void BackButton::setOpacity(float64 opacity) {
	_opacity = opacity;
	if (_widget) {
		if (opacity < 1.) {
			if (!_opacityEffect) {
				_opacityEffect = Ui::CreateChild<QGraphicsOpacityEffect>(
					_widget);
				_widget->setGraphicsEffect(_opacityEffect);
			}
			_opacityEffect->setOpacity(opacity);
		} else {
			_widget->setGraphicsEffect(nullptr);
			_opacityEffect = nullptr;
		}
	}
	update();
}

int BackButton::resizeGetHeight(int newWidth) {
	_cachedWidth = -1;
	return st::profileTopBarHeight;
}

void BackButton::updateCache() {
	if (_cachedWidth == width()) {
		return;
	}
	_cachedWidth = width();
	const auto widgetWidth = _widget
		? _widget->width() + st::historyAdminLogTopBarUserpicSkip
		: 0;
	const auto availableWidth = width()
		- st::historyAdminLogTopBarLeft
		- widgetWidth;
	_cachedElidedText = st::semiboldFont->elided(
		_text,
		availableWidth);
	_cachedElidedSubtext = st::dialogsTextFont->elided(
		_subtext,
		availableWidth);
}

void BackButton::paintEvent(QPaintEvent *e) {
	updateCache();

	auto p = Painter(this);

	p.fillRect(e->rect(), st::profileBg);
	st::topBarBack.paint(
		p,
		st::historyAdminLogTopBarLeft,
		(st::topBarHeight - st::topBarBack.height()) / 2,
		width());
	p.setOpacity(_opacity);

	const auto textHeight = st::semiboldFont->height;
	const auto subtextHeight = st::dialogsTextFont->height;
	const auto totalHeight = _subtext.isEmpty()
		? textHeight
		: textHeight + subtextHeight;
	const auto startY = (height() - totalHeight) / 2 - st::lineWidth;
	const auto widgetWidth = _widget
		? _widget->width() + st::historyAdminLogTopBarUserpicSkip
		: 0;
	const auto textX = st::historyAdminLogTopBarLeft + widgetWidth;

	p.setFont(st::semiboldFont);
	p.setPen(st::dialogsNameFg);
	p.drawTextLeft(textX, startY, width(), _cachedElidedText);

	if (!_subtext.isEmpty()) {
		p.setFont(st::dialogsTextFont);
		p.setPen(st::historyStatusFg);
		p.drawTextLeft(
			textX,
			startY + textHeight + st::lineWidth * 2,
			width(),
			_cachedElidedSubtext);
	}
}

void BackButton::onStateChanged(State was, StateChangeSource source) {
	if (isDown() && !(was & StateFlag::Down)) {
		clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
	}
}

} // namespace Profile
