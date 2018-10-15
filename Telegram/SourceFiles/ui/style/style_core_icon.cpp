/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

QImage createIconMask(const IconMask *mask, int scale) {
	auto maskImage = QImage::fromData(mask->data(), mask->size(), "PNG");
	maskImage.setDevicePixelRatio(cRetinaFactor());
	Assert(!maskImage.isNull());

	// images are layouted like this:
	// 100x 200x
	// 300x
	scale *= cIntRetinaFactor();
	const auto width = maskImage.width() / 3;
	const auto height = maskImage.height() / 5;
	const auto one = QRect(0, 0, width, height);
	const auto two = QRect(width, 0, width * 2, height * 2);
	const auto three = QRect(0, height * 2, width * 3, height * 3);
	if (scale == 100) {
		return maskImage.copy(one);
	} else if (scale == 200) {
		return maskImage.copy(two);
	} else if (scale == 300) {
		return maskImage.copy(three);
	}
	return maskImage.copy(
		(scale > 200) ? three : two
	).scaled(
		ConvertScale(width, scale),
		ConvertScale(height, scale),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
}

QSize readGeneratedSize(const IconMask *mask, int scale) {
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

			return QSize(
				ConvertScale(width, scale),
				ConvertScale(height, scale));
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

void MonoIcon::paint(
		QPainter &p,
		const QPoint &pos,
		int outerw,
		const style::palette &paletteOverride) const {
	auto size = readGeneratedSize(_mask, cScale());
	auto maskImage = QImage();
	if (size.isEmpty()) {
		maskImage = createIconMask(_mask, cScale());
		size = maskImage.size() / cIntRetinaFactor();
	}

	const auto w = size.width();
	const auto h = size.height();
	const auto fullOffset = pos + offset();
	const auto partPosX = rtl() ? (outerw - fullOffset.x() - w) : fullOffset.x();
	const auto partPosY = fullOffset.y();

	if (!maskImage.isNull()) {
		auto colorizedImage = QImage(
			maskImage.size(),
			QImage::Format_ARGB32_Premultiplied);
		colorizeImage(maskImage, _color[paletteOverride]->c, &colorizedImage);
		p.drawImage(partPosX, partPosY, colorizedImage);
	} else {
		p.fillRect(partPosX, partPosY, w, h, _color[paletteOverride]);
	}
}

void MonoIcon::fill(
		QPainter &p,
		const QRect &rect,
		const style::palette &paletteOverride) const {
	auto size = readGeneratedSize(_mask, cScale());
	auto maskImage = QImage();
	if (size.isEmpty()) {
		maskImage = createIconMask(_mask, cScale());
		size = maskImage.size() / cIntRetinaFactor();
	}
	if (!maskImage.isNull()) {
		auto colorizedImage = QImage(
			maskImage.size(),
			QImage::Format_ARGB32_Premultiplied);
		colorizeImage(maskImage, _color[paletteOverride]->c, &colorizedImage);
		p.drawImage(rect, colorizedImage, colorizedImage.rect());
	} else {
		p.fillRect(rect, _color[paletteOverride]);
	}
}

QImage MonoIcon::instance(QColor colorOverride, int scale) const {
	if (scale == kInterfaceScaleAuto) {
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

QImage IconData::instance(QColor colorOverride, int scale) const {
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
