/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/cross_fade_label.h"

#include "styles/style_widgets.h"

namespace Ui {
namespace {

constexpr auto kDuration = crl::time(200);

} // namespace

CrossFadeLabel::CrossFadeLabel(
	QWidget *parent,
	const style::LabelSimple &st)
: RpWidget(parent)
, _st(st) {
}

void CrossFadeLabel::setText(
		const QString &text,
		anim::type animated) {
	if (_current == text) {
		return;
	}
	_previous = _current;
	_previousWidth = _currentWidth;
	_current = text;
	_currentWidth = _st.font->width(_current);
	if (!height()) {
		resize(width(), _st.font->height);
	}
	if (animated == anim::type::instant) {
		_animation.stop();
		update();
		return;
	}
	_animation.start(
		[=] { update(); },
		0.,
		1.,
		kDuration,
		anim::easeOutCubic);
}

void CrossFadeLabel::setDirection(int direction) {
	_direction = direction;
}

void CrossFadeLabel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setPen(_st.textFg);
	p.setFont(_st.font);
	const auto slide = _st.font->height;
	const auto progress = _animation.value(1.);
	const auto y = _st.font->ascent;
	if (progress < 1. && !_previous.isEmpty()) {
		p.setOpacity(1. - progress);
		const auto dx = int(slide * _direction * progress);
		p.drawText(
			(width() - _previousWidth) / 2 + dx,
			y,
			_previous);
	}
	if (!_current.isEmpty()) {
		p.setOpacity(progress);
		const auto dx = int(slide * -_direction * (1. - progress));
		p.drawText(
			(width() - _currentWidth) / 2 + dx,
			y,
			_current);
	}
}

} // namespace Ui
