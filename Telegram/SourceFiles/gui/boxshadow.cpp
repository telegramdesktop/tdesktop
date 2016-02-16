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

#include "boxshadow.h"

BoxShadow::BoxShadow(const style::sprite &topLeft) : _size(topLeft.pxWidth()), _pixsize(_size * cIntRetinaFactor()) {
	if (!_size) return;

	QImage cornersImage(_pixsize * 2, _pixsize * 2, QImage::Format_ARGB32_Premultiplied);
	cornersImage.setDevicePixelRatio(cRetinaFactor());
	{
		QPainter p(&cornersImage);
		p.drawPixmap(QPoint(rtl() ? _size : 0, 0), App::sprite(), topLeft);
	}
	if (rtl()) cornersImage = cornersImage.mirrored(true, false);
	uchar *bits = cornersImage.bits();
	if (bits) {
		for (
			quint32 *p = (quint32*)bits, *end = (quint32*)(bits + cornersImage.byteCount());
			p < end;
			++p
		) {
			*p = (*p ^ 0x00ffffff) << 24;
		}
	}
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
	_corners = QPixmap::fromImage(cornersImage, Qt::ColorOnly);
	_corners.setDevicePixelRatio(cRetinaFactor());
	_colors.reserve(_pixsize);
	uchar prev = 0;
	for (int32 i = 0; i < _pixsize; ++i) {
		uchar a = (cornersImage.pixel(QPoint(i, _pixsize - 1)) >> 24);
		if (a < prev) break;

		_colors.push_back(style::color(0, 0, 0, a));
		prev = a;
	}
	if (cRetina()) {
		_left = QPixmap::fromImage(cornersImage.copy(0, _pixsize - 1, _colors.size(), 1), Qt::ColorOnly);
		_left.setDevicePixelRatio(cRetinaFactor());
		_top = QPixmap::fromImage(cornersImage.copy(_pixsize - 1, 0, 1, _colors.size()), Qt::ColorOnly);
		_top.setDevicePixelRatio(cRetinaFactor());
		_right = QPixmap::fromImage(cornersImage.copy(_pixsize * 2 - _colors.size(), _pixsize, _colors.size(), 1), Qt::ColorOnly);
		_right.setDevicePixelRatio(cRetinaFactor());
		_bottom = QPixmap::fromImage(cornersImage.copy(_pixsize, _pixsize * 2 - _colors.size(), 1, _colors.size()), Qt::ColorOnly);
		_bottom.setDevicePixelRatio(cRetinaFactor());
	}
}

void BoxShadow::paint(QPainter &p, const QRect &box, int32 shifty, int32 flags) {
	if (!_size) return;

	int32 rshifty = shifty * cIntRetinaFactor();
	int32 count = _colors.size(), countsize = count / cIntRetinaFactor(), minus = _size - countsize + shifty;
	bool left = (flags & Left), top = (flags & Top), right = (flags & Right), bottom = (flags & Bottom);
	if (left && top) p.drawPixmap(box.left() - _size + minus, box.top() - _size + minus + shifty, _corners, 0, 0, _pixsize, _pixsize);
	if (right && top) p.drawPixmap(box.left() + box.width() - minus, box.top() - _size + minus + shifty, _corners, _pixsize, 0, _pixsize, _pixsize);
	if (right && bottom) p.drawPixmap(box.left() + box.width() - minus, box.top() + box.height() - minus + shifty, _corners, _pixsize, _pixsize, _pixsize, _pixsize);
	if (left && bottom) p.drawPixmap(box.left() - _size + minus, box.top() + box.height() - minus + shifty, _corners, 0, _pixsize, _pixsize, _pixsize);
	if (cRetina()) {
		bool wasSmooth = p.renderHints().testFlag(QPainter::SmoothPixmapTransform);
		if (wasSmooth) p.setRenderHint(QPainter::SmoothPixmapTransform, false);
		if (left) p.drawPixmap(box.left() - countsize + shifty, box.top() + (top ? minus : 0) + shifty, countsize - shifty, box.height() - (bottom ? minus : 0) - (top ? minus : 0), _left, 0, 0, count - rshifty, 1);
		if (top) p.drawPixmap(box.left() + (left ? minus : 0), box.top() - countsize + 2 * shifty, box.width() - (right ? minus : 0) - (left ? minus : 0), countsize - 2 * shifty, _top, 0, 0, 1, count - 2 * rshifty);
		if (right) p.drawPixmap(box.left() + box.width(), box.top() + (top ? minus : 0) + shifty, countsize - shifty, box.height() - (bottom ? minus : 0) - (top ? minus : 0), _right, rshifty, 0, count - rshifty, 1);
		if (bottom) p.drawPixmap(box.left() + (left ? minus : 0), box.top() + box.height(), box.width() - (right ? minus : 0) - (left ? minus : 0), countsize, _bottom, 0, 0, 1, count);
		if (wasSmooth) p.setRenderHint(QPainter::SmoothPixmapTransform);
	} else {
		p.setPen(Qt::NoPen);
		for (int32 i = 0; i < count; ++i) {
			if (left && i + shifty < count) p.fillRect(box.left() - count + i + shifty, box.top() + (top ? minus : 0) + shifty, 1, box.height() - (bottom ? minus : 0) - (top ? minus : 0), _colors[i]->b);
			if (top && i + 2 * shifty < count) p.fillRect(box.left() + (left ? minus : 0), box.top() - count + i + 2 * shifty, box.width() - (right ? minus : 0) - (left ? minus : 0), 1, _colors[i]->b);
			if (right && i + shifty < count) p.fillRect(box.left() + box.width() + count - i - shifty - 1, box.top() + (top ? minus : 0) + shifty, 1, box.height() - (bottom ? minus : 0) - (top ? minus : 0), _colors[i]->b);
			if (bottom) p.fillRect(box.left() + (left ? minus : 0), box.top() + box.height() + count - i - 1, box.width() - (right ? minus : 0) - (left ? minus : 0), 1, _colors[i]->b);
		}
	}
}

style::margins BoxShadow::getDimensions(int32 shifty) const {
	int32 d = _colors.size() / cIntRetinaFactor();
	return style::margins(d - shifty, d - 2 * shifty, d - shifty, d);
}
