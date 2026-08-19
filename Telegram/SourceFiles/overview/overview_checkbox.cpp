/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "overview/overview_checkbox.h"

#include "styles/style_overview.h"
#include "styles/style_widgets.h"

namespace Overview::Layout {

void Checkbox::paint(
		QPainter &p,
		QPoint position,
		int outerWidth,
		bool selected,
		bool selecting) {
	_check.setDisplayInactive(selecting);
	_check.setChecked(selected);
	const auto pression = _pression.value((_active && _pressed) ? 1. : 0.);
	const auto scale = 1. - (1. - st::overviewCheckPressedSize) * pression;
	_check.paint(p, position.x(), position.y(), outerWidth, scale);
}

void Checkbox::setActive(bool active) {
	_active = active;
	if (_pressed) {
		startAnimation();
	}
}

void Checkbox::setPressed(bool pressed) {
	_pressed = pressed;
	if (_active) {
		startAnimation();
	}
}

void Checkbox::setChecked(bool checked, anim::type animated) {
	_check.setChecked(checked, animated);
}

void Checkbox::startAnimation() {
	const auto showPressed = (_pressed && _active);
	_pression.start(
		_updateCallback,
		showPressed ? 0. : 1.,
		showPressed ? 1. : 0.,
		st::overviewCheck.duration);
}

void Checkbox::finishAnimating() {
	_pression.stop();
	_check.finishAnimating();
}

} // namespace Overview::Layout
