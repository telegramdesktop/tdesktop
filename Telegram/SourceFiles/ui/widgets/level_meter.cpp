/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/level_meter.h"

#include "ui/painter.h"

namespace Ui {

LevelMeter::LevelMeter(QWidget *parent, const style::LevelMeter &st)
: RpWidget(parent)
, _st(st) {
}

void LevelMeter::setValue(float value) {
	_value = value;
	repaint();
}

void LevelMeter::paintEvent(QPaintEvent* event) {
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);

	p.setPen(Qt::NoPen);

	const auto activeFg = _st.activeFg;
	const auto inactiveFg = _st.inactiveFg;
	const auto radius = _st.lineWidth / 2;
	const auto rect = QRect(0, 0, _st.lineWidth, height());
	p.setBrush(activeFg);
	for (auto i = 0; i < _st.lineCount; ++i) {
		const auto valueAtLine = (float)(i + 1) / _st.lineCount;
		if (valueAtLine > _value) {
			p.setBrush(inactiveFg);
		}
		p.drawRoundedRect(
			rect.translated((_st.lineWidth + _st.lineSpacing) * i, 0),
			radius,
			radius);
	}
}

} // namespace Ui
