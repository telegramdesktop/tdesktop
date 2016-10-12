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
#include "ui/effects/rect_shadow.h"

namespace Ui {

RectShadow::RectShadow(const style::icon &topLeft) : _size(topLeft.width()), _pixsize(_size * cIntRetinaFactor()) {
	if (!_size) return;

	QImage cornersImage(_pixsize * 2, _pixsize * 2, QImage::Format_ARGB32_Premultiplied);
	cornersImage.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&cornersImage);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		topLeft.paint(p, QPoint(0, 0), _size);
	}
	if (rtl()) cornersImage = cornersImage.mirrored(true, false);
	{
		QPainter p(&cornersImage);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		QImage m = cornersImage.mirrored();
		m.setDevicePixelRatio(cRetinaFactor());
		p.drawImage(0, _size, m, 0, _pixsize, _pixsize, _pixsize);
	}
	{
		QPainter p(&cornersImage);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		QImage m = cornersImage.mirrored(true, false);
		m.setDevicePixelRatio(cRetinaFactor());
		p.drawImage(_size, 0, m, _pixsize, 0, _pixsize, _pixsize * 2);
	}

	uchar prev = 0;
	for (int i = 0; i < _pixsize; ++i) {
		uchar a = (cornersImage.pixel(QPoint(i, _pixsize - 1)) >> 24);
		if (a < prev) break;

		++_thickness;
		prev = a;
	}

	_left = App::pixmapFromImageInPlace(cornersImage.copy(0, _pixsize - 1, _thickness, 1));
	_left.setDevicePixelRatio(cRetinaFactor());
	_top = App::pixmapFromImageInPlace(cornersImage.copy(_pixsize - 1, 0, 1, _thickness));
	_top.setDevicePixelRatio(cRetinaFactor());
	_right = App::pixmapFromImageInPlace(cornersImage.copy(_pixsize * 2 - _thickness, _pixsize, _thickness, 1));
	_right.setDevicePixelRatio(cRetinaFactor());
	_bottom = App::pixmapFromImageInPlace(cornersImage.copy(_pixsize, _pixsize * 2 - _thickness, 1, _thickness));
	_bottom.setDevicePixelRatio(cRetinaFactor());

	_corners = App::pixmapFromImageInPlace(std_::move(cornersImage));
	_corners.setDevicePixelRatio(cRetinaFactor());
}

void RectShadow::paint(Painter &p, const QRect &box, int shifty, Sides sides) {
	if (!_size) return;

	int32 rshifty = shifty * cIntRetinaFactor();
	int32 count = _thickness, countsize = count / cIntRetinaFactor(), minus = _size - countsize + shifty;
	bool left = (sides & Side::Left), top = (sides & Side::Top), right = (sides & Side::Right), bottom = (sides & Side::Bottom);
	if (left && top) p.drawPixmap(box.left() - _size + minus, box.top() - _size + minus + shifty, _corners, 0, 0, _pixsize, _pixsize);
	if (right && top) p.drawPixmap(box.left() + box.width() - minus, box.top() - _size + minus + shifty, _corners, _pixsize, 0, _pixsize, _pixsize);
	if (right && bottom) p.drawPixmap(box.left() + box.width() - minus, box.top() + box.height() - minus + shifty, _corners, _pixsize, _pixsize, _pixsize, _pixsize);
	if (left && bottom) p.drawPixmap(box.left() - _size + minus, box.top() + box.height() - minus + shifty, _corners, 0, _pixsize, _pixsize, _pixsize);

	bool wasSmooth = p.renderHints().testFlag(QPainter::SmoothPixmapTransform);
	if (wasSmooth) p.setRenderHint(QPainter::SmoothPixmapTransform, false);
	if (left) p.drawPixmap(box.left() - countsize + shifty, box.top() + (top ? (minus + shifty) : 0), countsize - shifty, box.height() - (bottom ? (minus - shifty) : 0) - (top ? (minus + shifty) : 0), _left, 0, 0, count - rshifty, 1);
	if (top) p.drawPixmap(box.left() + (left ? minus : 0), box.top() - countsize + 2 * shifty, box.width() - (right ? minus : 0) - (left ? minus : 0), countsize - 2 * shifty, _top, 0, 0, 1, count - 2 * rshifty);
	if (right) p.drawPixmap(box.left() + box.width(), box.top() + (top ? (minus + shifty) : 0), countsize - shifty, box.height() - (bottom ? (minus - shifty) : 0) - (top ? (minus + shifty) : 0), _right, rshifty, 0, count - rshifty, 1);
	if (bottom) p.drawPixmap(box.left() + (left ? minus : 0), box.top() + box.height(), box.width() - (right ? minus : 0) - (left ? minus : 0), countsize, _bottom, 0, 0, 1, count);
	if (wasSmooth) p.setRenderHint(QPainter::SmoothPixmapTransform);
}

style::margins RectShadow::getDimensions(int32 shifty) const {
	if (!_size) return style::margins(0, 0, 0, 0);

	int d = _thickness / cIntRetinaFactor();
	return style::margins(d - shifty, d - 2 * shifty, d - shifty, d);
}

} // namespace Ui
