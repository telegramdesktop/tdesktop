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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "ui/style/style_core_icon.h"

namespace style {
namespace internal {
namespace {

uint32 colorKey(QColor c) {
	return (((((uint32(c.red()) << 8) | uint32(c.green())) << 8) | uint32(c.blue())) << 8) | uint32(c.alpha());
}

using IconMasks = QMap<const IconMask*, QImage>;
using IconPixmaps = QMap<QPair<const IconMask*, uint32>, QPixmap>;
using IconDatas = OrderedSet<IconData*>;
NeverFreedPointer<IconMasks> iconMasks;
NeverFreedPointer<IconPixmaps> iconPixmaps;
NeverFreedPointer<IconDatas> iconData;

inline int pxAdjust(int value, int scale) {
	if (value < 0) {
		return -pxAdjust(-value, scale);
	}
	return qFloor((value * scale / 4.) + 0.1);
}

QImage createIconMask(const IconMask *mask, DBIScale scale) {
	auto maskImage = QImage::fromData(mask->data(), mask->size(), "PNG");
	maskImage.setDevicePixelRatio(cRetinaFactor());
	Assert(!maskImage.isNull());

	// images are layouted like this:
	// 200x 100x
	// 150x 125x
	int width = maskImage.width() / 3;
	int height = qRound((maskImage.height() * 2) / 7.);
	auto r = QRect(0, 0, width * 2, height * 2);
	if (!cRetina() && scale != dbisTwo) {
		if (scale == dbisOne) {
			r = QRect(width * 2, 0, width, height);
		} else {
			int width125 = pxAdjust(width, 5);
			int height125 = pxAdjust(height, 5);
			int width150 = pxAdjust(width, 6);
			int height150 = pxAdjust(height, 6);
			if (scale == dbisOneAndQuarter) {
				r = QRect(width150, height * 2, width125, height125);
			} else {
				r = QRect(0, height * 2, width150, height150);
			}
		}
	}
	return maskImage.copy(r);
}

QSize readGeneratedSize(const IconMask *mask, DBIScale scale) {
	auto data = mask->data();
	auto size = mask->size();

	auto generateTag = qstr("GENERATE:");
	if (size > generateTag.size() && !memcmp(data, generateTag.data(), generateTag.size())) {
		size -= generateTag.size();
		data += generateTag.size();
		auto sizeTag = qstr("SIZE:");
		if (size > sizeTag.size() && !memcmp(data, sizeTag.data(), sizeTag.size())) {
			size -= sizeTag.size();
			data += sizeTag.size();
			auto baForStream = QByteArray::fromRawData(reinterpret_cast<const char*>(data), size);
			QDataStream stream(baForStream);
			stream.setVersion(QDataStream::Qt_5_1);

			qint32 width = 0, height = 0;
			stream >> width >> height;
			Assert(stream.status() == QDataStream::Ok);

			switch (scale) {
			case dbisOne: return QSize(width, height);
			case dbisOneAndQuarter: return QSize(pxAdjust(width, 5), pxAdjust(height, 5));
			case dbisOneAndHalf: return QSize(pxAdjust(width, 6), pxAdjust(height, 6));
			case dbisTwo: return QSize(width * 2, height * 2);
			}
		} else {
			Unexpected("Bad data in generated icon!");
		}
	}
	return QSize();
}

} // namespace

MonoIcon::MonoIcon(const IconMask *mask, Color color, QPoint offset)
: _mask(mask)
, _color(std::move(color))
, _offset(offset) {
}

void MonoIcon::reset() const {
	_pixmap = QPixmap();
	_size = QSize();
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
		ensureColorizedImage(colorOverride);
		p.drawImage(partPosX, partPosY, _colorizedImage);
	}
}

void MonoIcon::fill(QPainter &p, const QRect &rect, QColor colorOverride) const {
	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(rect, colorOverride);
	} else {
		ensureColorizedImage(colorOverride);
		p.drawImage(rect, _colorizedImage, _colorizedImage.rect());
	}
}

void MonoIcon::paint(QPainter &p, const QPoint &pos, int outerw, const style::palette &paletteOverride) const {
	int w = width(), h = height();
	QPoint fullOffset = pos + offset();
	int partPosX = rtl() ? (outerw - fullOffset.x() - w) : fullOffset.x();
	int partPosY = fullOffset.y();

	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(partPosX, partPosY, w, h, _color[paletteOverride]);
	} else {
		ensureColorizedImage(_color[paletteOverride]->c);
		p.drawImage(partPosX, partPosY, _colorizedImage);
	}
}

void MonoIcon::fill(QPainter &p, const QRect &rect, const style::palette &paletteOverride) const {
	ensureLoaded();
	if (_pixmap.isNull()) {
		p.fillRect(rect, _color[paletteOverride]);
	} else {
		ensureColorizedImage(_color[paletteOverride]->c);
		p.drawImage(rect, _colorizedImage, _colorizedImage.rect());
	}
}

QImage MonoIcon::instance(QColor colorOverride, DBIScale scale) const {
	if (scale == dbisAuto) {
		ensureLoaded();
		auto result = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(cRetinaFactor());
		if (_pixmap.isNull()) {
			result.fill(colorOverride);
		} else {
			colorizeImage(_maskImage, colorOverride, &result);
		}
		return result;
	}
	auto size = readGeneratedSize(_mask, scale);
	if (!size.isEmpty()) {
		auto result = QImage(size * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(cRetinaFactor());
		result.fill(colorOverride);
		return result;
	}
	auto mask = createIconMask(_mask, scale);
	auto result = QImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	colorizeImage(mask, colorOverride, &result);
	return result;
}

void MonoIcon::ensureLoaded() const {
	if (_size.isValid()) {
		return;
	}
	if (!_maskImage.isNull()) {
		createCachedPixmap();
		return;
	}

	_size = readGeneratedSize(_mask, cScale());
	if (_size.isEmpty()) {
		iconMasks.createIfNull();
		auto i = iconMasks->constFind(_mask);
		if (i == iconMasks->cend()) {
			i = iconMasks->insert(_mask, createIconMask(_mask, cScale()));
		}
		_maskImage = i.value();

		createCachedPixmap();
	}
}

void MonoIcon::ensureColorizedImage(QColor color) const {
	if (_colorizedImage.isNull()) _colorizedImage = QImage(_maskImage.size(), QImage::Format_ARGB32_Premultiplied);
	colorizeImage(_maskImage, color, &_colorizedImage);
}

void MonoIcon::createCachedPixmap() const {
	iconPixmaps.createIfNull();
	auto key = qMakePair(_mask, colorKey(_color->c));
	auto j = iconPixmaps->constFind(key);
	if (j == iconPixmaps->cend()) {
		auto image = colorizeImage(_maskImage, _color);
		j = iconPixmaps->insert(key, App::pixmapFromImageInPlace(std::move(image)));
	}
	_pixmap = j.value();
	_size = _pixmap.size() / cIntRetinaFactor();
}

void IconData::created() {
	iconData.createIfNull();
	iconData->insert(this);
}

void IconData::fill(QPainter &p, const QRect &rect) const {
	if (_parts.empty()) return;

	auto partSize = _parts[0].size();
	for_const (auto &part, _parts) {
		Assert(part.offset() == QPoint(0, 0));
		Assert(part.size() == partSize);
		part.fill(p, rect);
	}
}

void IconData::fill(QPainter &p, const QRect &rect, QColor colorOverride) const {
	if (_parts.empty()) return;

	auto partSize = _parts[0].size();
	for_const (auto &part, _parts) {
		Assert(part.offset() == QPoint(0, 0));
		Assert(part.size() == partSize);
		part.fill(p, rect, colorOverride);
	}
}

QImage IconData::instance(QColor colorOverride, DBIScale scale) const {
	Assert(_parts.size() == 1);
	auto &part = _parts[0];
	Assert(part.offset() == QPoint(0, 0));
	return part.instance(colorOverride, scale);
}

int IconData::width() const {
	if (_width < 0) {
		_width = 0;
		for_const (auto &part, _parts) {
			accumulate_max(_width, part.offset().x() + part.width());
		}
	}
	return _width;
}

int IconData::height() const {
	if (_height < 0) {
		_height = 0;
		for_const (auto &part, _parts) {
			accumulate_max(_height, part.offset().x() + part.height());
		}
	}
	return _height;
}

void resetIcons() {
	iconPixmaps.clear();
	if (iconData) {
		for (auto data : *iconData) {
			data->reset();
		}
	}
}

void destroyIcons() {
	iconData.clear();
	iconPixmaps.clear();
	iconMasks.clear();
}

} // namespace internal
} // namespace style
