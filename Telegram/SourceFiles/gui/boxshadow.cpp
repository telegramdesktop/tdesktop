/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"

#include "boxshadow.h"

BoxShadow::BoxShadow(const style::rect &topLeft) : _size(topLeft.width() / cIntRetinaFactor()) {
	QImage cornersImage(_size * 2, _size * 2, QImage::Format_ARGB32_Premultiplied);
	{
		QPainter p(&cornersImage);
		p.drawPixmap(QPoint(0, 0), App::sprite(), topLeft);
	}
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
		p.drawImage(0, _size, cornersImage.mirrored(), 0, _size, _size, _size);
	}
	{
		QPainter p(&cornersImage);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.drawImage(_size, 0, cornersImage.mirrored(true, false), _size, 0, _size, _size * 2);
	}
	_corners = QPixmap::fromImage(cornersImage);
	_colors.reserve(_size);
	uchar prev = 0;
	for (int32 i = 0; i < _size; ++i) {
		uchar a = (cornersImage.pixel(QPoint(i, _size - 1)) >> 24);
		if (a < prev) break;

		_colors.push_back(style::color(0, 0, 0, a));
		prev = a;
	}
}

void BoxShadow::paint(QPainter &p, const QRect &box, const QPoint &shift, int32 flags) {
	int32 count = _colors.size(), minus = _size - count + 1;
	bool left = (flags & Left), top = (flags & Top), right = (flags & Right), bottom = (flags & Bottom);
	if (left && top) p.drawPixmap(box.left() - _size + minus + shift.x(), box.top() - _size + minus + shift.y(), _corners, 0, 0, _size, _size);
	if (right && top) p.drawPixmap(box.right() - minus + 1 + shift.x(), box.top() - _size + minus + shift.y(), _corners, _size, 0, _size, _size);
	if (right && bottom) p.drawPixmap(box.right() - minus + 1 + shift.x(), box.bottom() - minus + 1 + shift.y(), _corners, _size, _size, _size, _size);
	if (left && bottom) p.drawPixmap(box.left() - _size + minus + shift.x(), box.bottom() - minus + 1 + shift.y(), _corners, 0, _size, _size, _size);
	for (int32 i = 1; i <= count; ++i) {
		p.setPen(_colors[i - 1]->p);
		if (top) p.fillRect(box.left() + (left ? minus : 0) + shift.x(), box.top() - count + i + shift.y(), box.width() - (right ? minus : 0) - (left ? minus : 0), st::lineWidth, _colors[i - 1]->b);
		if (right) p.fillRect(box.right() + count - i + shift.x(), box.top() + (top ? minus : 0) + shift.y(), st::lineWidth, box.height() - (bottom ? minus : 0) - (top ? minus : 0), _colors[i - 1]->b);
		if (bottom) p.fillRect(box.left() + (left ? minus : 0) + shift.x(), box.bottom() + count - i + shift.y(), box.width() - (right ? minus : 0) - (left ? minus : 0), st::lineWidth, _colors[i - 1]->b);
		if (left) p.fillRect(box.left() - count + i + shift.x(), box.top() + (top ? minus : 0) + shift.y(), st::lineWidth, box.height() - (bottom ? minus : 0) - (top ? minus : 0), _colors[i - 1]->b);
	}
}
