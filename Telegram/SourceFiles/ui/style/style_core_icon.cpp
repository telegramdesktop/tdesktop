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
#include "ui/style/style_core_icon.h"

namespace style {
namespace internal {
namespace {

uint32 colorKey(QColor c) {
	return (((((uint32(c.red()) << 8) | uint32(c.green())) << 8) | uint32(c.blue())) << 8) | uint32(c.alpha());
}

using IconMasks = QMap<const IconMask*, QImage>;
using IconPixmaps = QMap<QPair<const IconMask*, uint32>, QPixmap>;
NeverFreedPointer<IconMasks> iconMasks;
NeverFreedPointer<IconPixmaps> iconPixmaps;

inline int pxAdjust(int value, int scale) {
	if (value < 0) {
		return -pxAdjust(-value, scale);
	}
	return qFloor((value * scale / 4.) + 0.1);
}

QImage createIconMask(const IconMask *mask) {
	auto maskImage = QImage::fromData(mask->data(), mask->size(), "PNG");
	maskImage.setDevicePixelRatio(cRetinaFactor());
	t_assert(!maskImage.isNull());

	// images are layouted like this:
	// 200x 100x
	// 150x 125x
	int width = maskImage.width() / 3;
	int height = qRound((maskImage.height() * 2) / 7.);
	auto r = QRect(0, 0, width * 2, height * 2);
	if (!cRetina() && cScale() != dbisTwo) {
		if (cScale() == dbisOne) {
			r = QRect(width * 2, 0, width, height);
		} else {
			int width125 = pxAdjust(width, 5);
			int height125 = pxAdjust(height, 5);
			int width150 = pxAdjust(width, 6);
			int height150 = pxAdjust(height, 6);
			if (cScale() == dbisOneAndQuarter) {
				r = QRect(width150, height * 2, width125, height125);
			} else {
				r = QRect(0, height * 2, width150, height150);
			}
		}
	}
	return maskImage.copy(r);
}

QImage createIconImage(const QImage &mask, QColor color) {
	return colorizeImage(mask, color, QRect(0, 0, mask.width(), mask.height()));
}

} // namespace

MonoIcon::MonoIcon(const IconMask *mask, const Color &color, QPoint offset)
: _mask(mask)
, _color(color)
, _offset(offset) {
}

int MonoIcon::width() const {
	ensureLoaded();
	return _size.width();
}

int MonoIcon::height() const {
	ensureLoaded();
	return _size.height();
}

QSize MonoIcon::size() const {
	ensureLoaded();
	return _size;
}

QPoint MonoIcon::offset() const {
	return _offset;
}

void MonoIcon::paint(QPainter &p, const QPoint &pos, int outerw) const {
	int w = width(), h = height();
	QPoint fullOffset = pos + offset();
	int partPosX = rtl() ? (outerw - fullOffset.x() - w) : fullOffset.x();
	int partPosY = fullOffset.y();

	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(partPosX, partPosY, w, h, _color);
	} else {
		p.drawPixmap(partPosX, partPosY, _pixmap);
	}
}

void MonoIcon::fill(QPainter &p, const QRect &rect) const {
	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(rect, _color);
	} else {
		p.drawPixmap(rect, _pixmap, QRect(0, 0, _pixmap.width(), _pixmap.height()));
	}
}

void MonoIcon::paint(QPainter &p, const QPoint &pos, int outerw, QColor colorOverride) const {
	int w = width(), h = height();
	QPoint fullOffset = pos + offset();
	int partPosX = rtl() ? (outerw - fullOffset.x() - w) : fullOffset.x();
	int partPosY = fullOffset.y();

	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(partPosX, partPosY, w, h, colorOverride);
	} else {
		p.drawImage(partPosX, partPosY, createIconImage(_maskImage, colorOverride));
	}
}

void MonoIcon::fill(QPainter &p, const QRect &rect, QColor colorOverride) const {
	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(rect, colorOverride);
	} else {
		p.drawImage(rect, createIconImage(_maskImage, colorOverride), QRect(0, 0, _pixmap.width(), _pixmap.height()));
	}
}

void MonoIcon::ensureLoaded() const {
	if (_size.isValid()) {
		return;
	}
	const uchar *data = _mask->data();
	int size = _mask->size();

	auto generateTag = qstr("GENERATE:");
	if (size > generateTag.size() && !memcmp(data, generateTag.data(), generateTag.size())) {
		size -= generateTag.size();
		data += generateTag.size();
		auto sizeTag = qstr("SIZE:");
		if (size > sizeTag.size() && !memcmp(data, sizeTag.data(), sizeTag.size())) {
			size -= sizeTag.size();
			data += sizeTag.size();
			auto baForStream = QByteArray::fromRawData(reinterpret_cast<const char*>(data), size);
			QBuffer buffer(&baForStream);
			buffer.open(QIODevice::ReadOnly);

			QDataStream stream(&buffer);
			stream.setVersion(QDataStream::Qt_5_1);

			qint32 width = 0, height = 0;
			stream >> width >> height;
			t_assert(stream.status() == QDataStream::Ok);

			switch (cScale()) {
			case dbisOne: _size = QSize(width, height); break;
			case dbisOneAndQuarter: _size = QSize(pxAdjust(width, 5), pxAdjust(height, 5)); break;
			case dbisOneAndHalf: _size = QSize(pxAdjust(width, 6), pxAdjust(height, 6)); break;
			case dbisTwo: _size = QSize(width * 2, height * 2); break;
			}
		} else {
			t_assert(!"Bad data in generated icon!");
		}
	} else {
		iconMasks.createIfNull();
		auto i = iconMasks->constFind(_mask);
		if (i == iconMasks->cend()) {
			i = iconMasks->insert(_mask, createIconMask(_mask));
		}
		_maskImage = i.value();

		iconPixmaps.createIfNull();
		auto key = qMakePair(_mask, colorKey(_color->c));
		auto j = iconPixmaps->constFind(key);
		if (j == iconPixmaps->cend()) {
			j = iconPixmaps->insert(key, App::pixmapFromImageInPlace(createIconImage(_maskImage, _color->c)));
		}
		_pixmap = j.value();
		_size = _pixmap.size() / cIntRetinaFactor();
	}
}

void Icon::fill(QPainter &p, const QRect &rect) const {
	if (_parts.isEmpty()) return;

	auto partSize = _parts.at(0).size();
	for_const (auto &part, _parts) {
		t_assert(part.offset() == QPoint(0, 0));
		t_assert(part.size() == partSize);
		part.fill(p, rect);
	}
}

void Icon::fill(QPainter &p, const QRect &rect, QColor colorOverride) const {
	if (_parts.isEmpty()) return;

	auto partSize = _parts.at(0).size();
	for_const (auto &part, _parts) {
		t_assert(part.offset() == QPoint(0, 0));
		t_assert(part.size() == partSize);
		part.fill(p, rect, colorOverride);
	}
}

int Icon::width() const {
	if (_width < 0) {
		_width = 0;
		for_const (auto &part, _parts) {
			accumulate_max(_width, part.offset().x() + part.width());
		}
	}
	return _width;
}

int Icon::height() const {
	if (_height < 0) {
		_height = 0;
		for_const (auto &part, _parts) {
			accumulate_max(_height, part.offset().x() + part.height());
		}
	}
	return _height;
}

void destroyIcons() {
	iconPixmaps.clear();
	iconMasks.clear();
}

} // namespace internal
} // namespace style
