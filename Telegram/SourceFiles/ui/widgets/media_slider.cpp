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
#include "ui/widgets/media_slider.h"

#include "styles/style_widgets.h"

namespace Ui {

MediaSlider::MediaSlider(QWidget *parent, const style::MediaSlider &st) : ContinuousSlider(parent)
, _st(st) {
}

QRect MediaSlider::getSeekRect() const {
	return isHorizontal()
		? QRect(_st.seekSize.width() / 2, 0, width() - _st.seekSize.width(), height())
		: QRect(0, _st.seekSize.height() / 2, width(), height() - _st.seekSize.width());
}

float64 MediaSlider::getOverDuration() const {
	return _st.duration;
}

void MediaSlider::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.setPen(Qt::NoPen);
	p.setRenderHint(QPainter::HighQualityAntialiasing);

	auto horizontal = isHorizontal();
	auto ms = getms();
	auto masterOpacity = fadeOpacity();
	auto radius = _st.width / 2;
	auto disabled = isDisabled();
	auto over = getCurrentOverFactor(ms);
	auto seekRect = getSeekRect();
	auto value = getCurrentValue(ms);

	// invert colors and value for vertical
	if (!horizontal) value = 1. - value;

	auto markerFrom = (horizontal ? seekRect.x() : seekRect.y());
	auto markerLength = (horizontal ? seekRect.width() : seekRect.height());
	auto from = _alwaysDisplayMarker ? 0 : markerFrom;
	auto length = _alwaysDisplayMarker ? (horizontal ? width() : height()) : markerLength;
	auto mid = disabled ? from : qRound(from + value * length);
	auto end = from + length;
	if (mid > from) {
		auto fromClipRect = horizontal ? QRect(0, 0, mid, height()) : QRect(0, 0, width(), mid);
		auto fromRect = horizontal
			? QRect(from, (height() - _st.width) / 2, mid + radius - from, _st.width)
			: QRect((width() - _st.width) / 2, from, _st.width, mid + radius - from);
		p.setClipRect(fromClipRect);
		p.setOpacity(masterOpacity * (over * _st.activeOpacity + (1. - over) * _st.inactiveOpacity));
		p.setBrush(horizontal ? _st.activeFg : _st.inactiveFg);
		p.drawRoundedRect(fromRect, radius, radius);
	}
	if (end > mid) {
		auto endClipRect = horizontal ? QRect(mid, 0, width() - mid, height()) : QRect(0, mid, width(), height() - mid);
		auto endRect = horizontal
			? QRect(mid - radius, (height() - _st.width) / 2, end - (mid - radius), _st.width)
			: QRect((width() - _st.width) / 2, mid - radius, _st.width, end - (mid - radius));
		p.setClipRect(endClipRect);
		p.setOpacity(masterOpacity);
		p.setBrush(horizontal ? _st.inactiveFg : _st.activeFg);
		p.drawRoundedRect(endRect, radius, radius);
	}
	auto markerSizeRatio = disabled ? 0. : (_alwaysDisplayMarker ? 1. : over);
	if (markerSizeRatio > 0) {
		auto position = qRound(markerFrom + value * markerLength) - (horizontal ? seekRect.x() : seekRect.y());
		auto seekButton = horizontal
			? QRect(position, (height() - _st.seekSize.height()) / 2, _st.seekSize.width(), _st.seekSize.height())
			: QRect((width() - _st.seekSize.width()) / 2, position, _st.seekSize.width(), _st.seekSize.height());
		auto size = horizontal ? _st.seekSize.width() : _st.seekSize.height();
		auto remove = static_cast<int>(((1. - markerSizeRatio) * size) / 2.);
		if (remove * 2 < size) {
			p.setClipRect(rect());
			p.setOpacity(masterOpacity * _st.activeOpacity);
			p.setBrush(_st.activeFg);
			p.drawEllipse(seekButton.marginsRemoved(QMargins(remove, remove, remove, remove)));
		}
	}
}

} // namespace Ui
