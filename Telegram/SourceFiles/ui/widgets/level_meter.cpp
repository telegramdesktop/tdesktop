/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/level_meter.h"

#include "ui/painter.h"
#include "styles/style_widgets.h"

namespace Ui {

LevelMeter::LevelMeter(QWidget *parent, const style::LevelMeter &st)
: RpWidget(parent)
, _st(st)
, _animation([=](crl::time now) {
	return animationCallback(now);
}) {
}

void LevelMeter::animateTo(float value, crl::time duration) {
	_value.start(value);
	_duration = duration * anim::SlowMultiplier();
	_animation.start();
}

void LevelMeter::stopAnimation() {
	_animation.stop();
}

bool LevelMeter::animationCallback(crl::time now) {
	const auto time = now - _animation.started();
	const auto dt = anim::Disabled()
		? 1.
		: (time / float64(_duration));
	if (dt >= 1.) {
		_value.finish();
	} else {
		_value.update(dt, anim::linear);
	}
	repaint();
	return (dt < 1.);
}

void LevelMeter::paintEvent(QPaintEvent* event) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	p.setPen(Qt::NoPen);

	const auto activeFg = _st.activeFg;
	const auto inactiveFg = _st.inactiveFg;
	const auto radius = _st.lineWidth / 2;
	const auto rect = QRect(0, 0, _st.lineWidth, height());
	const auto value = float(_value.current());
	p.setBrush(activeFg);
	for (auto i = 0; i < _st.lineCount; ++i) {
		const auto valueAtLine = (float64)(i + 1) / _st.lineCount;
		if (valueAtLine > value) {
			p.setBrush(inactiveFg);
		}
		p.drawRoundedRect(
			rect.translated((_st.lineWidth + _st.lineSpacing) * i, 0),
			radius,
			radius);
	}
}

} // namespace Ui
