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
#include "ui/widgets/filled_slider.h"

#include "styles/style_widgets.h"

namespace Ui {

FilledSlider::FilledSlider(QWidget *parent, const style::FilledSlider &st) : ContinuousSlider(parent)
, _st(st) {
}

QRect FilledSlider::getSeekRect() const {
	return QRect(0, 0, width(), height());
}

float64 FilledSlider::getOverDuration() const {
	return _st.duration;
}

void FilledSlider::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.setPen(Qt::NoPen);
	p.setRenderHint(QPainter::HighQualityAntialiasing);

	auto masterOpacity = fadeOpacity();
	auto ms = getms();
	auto disabled = isDisabled();
	auto over = getCurrentOverFactor(ms);
	auto lineWidth = _st.lineWidth + ((_st.fullWidth - _st.lineWidth) * over);
	auto lineWidthRounded = qFloor(lineWidth);
	auto lineWidthPartial = lineWidth - lineWidthRounded;
	auto seekRect = getSeekRect();
	auto value = getCurrentValue(ms);
	auto from = seekRect.x(), mid = disabled ? from : qRound(from + value * seekRect.width()), end = from + seekRect.width();
	if (mid > from) {
		p.setOpacity(masterOpacity);
		p.fillRect(from, height() - lineWidthRounded, (mid - from), lineWidthRounded, _st.activeFg);
		if (lineWidthPartial > 0.01) {
			p.setOpacity(masterOpacity * lineWidthPartial);
			p.fillRect(from, height() - lineWidthRounded - 1, (mid - from), 1, _st.activeFg);
		}
	}
	if (end > mid && over > 0) {
		p.setOpacity(masterOpacity * over);
		p.fillRect(mid, height() - lineWidthRounded, (end - mid), lineWidthRounded, _st.inactiveFg);
		if (lineWidthPartial > 0.01) {
			p.setOpacity(masterOpacity * over * lineWidthPartial);
			p.fillRect(mid, height() - lineWidthRounded - 1, (end - mid), 1, _st.inactiveFg);
		}
	}
}

} // namespace Ui
