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
#include "ui/images.h"

#include "mainwidget.h"
#include "localstorage.h"

#include "pspecific.h"

namespace {
	typedef QMap<QString, Image*> LocalImages;
	LocalImages localImages;

	typedef QMap<QString, WebImage*> WebImages;
	WebImages webImages;

	Image *blank() {
		static Image *img = internal::getImage(qsl(":/gui/art/blank.gif"), "GIF");
		return img;
	}

	typedef QMap<StorageKey, StorageImage*> StorageImages;
	StorageImages storageImages;

	int64 globalAcquiredSize = 0;

	static const uint64 BlurredCacheSkip = 0x1000000000000000LLU;
	static const uint64 ColoredCacheSkip = 0x2000000000000000LLU;
	static const uint64 BlurredColoredCacheSkip = 0x3000000000000000LLU;
	static const uint64 RoundedCacheSkip = 0x4000000000000000LLU;
	static const uint64 CircledCacheSkip = 0x5000000000000000LLU;
}

StorageImageLocation StorageImageLocation::Null;

bool Image::isNull() const {
	return (this == blank());
}

ImagePtr::ImagePtr() : Parent(blank()) {
}

ImagePtr::ImagePtr(int32 width, int32 height, const MTPFileLocation &location, ImagePtr def) :
	Parent((location.type() == mtpc_fileLocation) ? (Image*)(internal::getImage(StorageImageLocation(width, height, location.c_fileLocation()))) : def.v()) {
}

Image::Image(const QString &file, QByteArray fmt) : _forgot(false) {
	_data = QPixmap::fromImage(App::readImage(file, &fmt, false, 0, &_saved), Qt::ColorOnly);
	_format = fmt;
	if (!_data.isNull()) {
		globalAcquiredSize += int64(_data.width()) * _data.height() * 4;
	}
}

Image::Image(const QByteArray &filecontent, QByteArray fmt) : _forgot(false) {
	_data = QPixmap::fromImage(App::readImage(filecontent, &fmt, false), Qt::ColorOnly);
	_format = fmt;
	_saved = filecontent;
	if (!_data.isNull()) {
		globalAcquiredSize += int64(_data.width()) * _data.height() * 4;
	}
}

Image::Image(const QPixmap &pixmap, QByteArray format) : _format(format), _forgot(false), _data(pixmap) {
	if (!_data.isNull()) {
		globalAcquiredSize += int64(_data.width()) * _data.height() * 4;
	}
}

Image::Image(const QByteArray &filecontent, QByteArray fmt, const QPixmap &pixmap) : _saved(filecontent), _format(fmt), _forgot(false), _data(pixmap) {
	_data = pixmap;
	_format = fmt;
	_saved = filecontent;
	if (!_data.isNull()) {
		globalAcquiredSize += int64(_data.width()) * _data.height() * 4;
	}
}

const QPixmap &Image::pix(int32 w, int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
        w = width();
    } else if (cRetina()) {
        w *= cIntRetinaFactor();
        h *= cIntRetinaFactor();
    }
	uint64 k = (uint64(w) << 32) | uint64(h);
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		QPixmap p(pixNoCache(w, h, ImagePixSmooth));
        if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixRounded(int32 w, int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width();
	} else if (cRetina()) {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	uint64 k = RoundedCacheSkip | (uint64(w) << 32) | uint64(h);
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		QPixmap p(pixNoCache(w, h, ImagePixSmooth | ImagePixRounded));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixCircled(int32 w, int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width();
	} else if (cRetina()) {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	uint64 k = CircledCacheSkip | (uint64(w) << 32) | uint64(h);
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		QPixmap p(pixNoCache(w, h, ImagePixSmooth | ImagePixCircled));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixBlurred(int32 w, int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else if (cRetina()) {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	uint64 k = BlurredCacheSkip | (uint64(w) << 32) | uint64(h);
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		QPixmap p(pixNoCache(w, h, ImagePixSmooth | ImagePixBlurred));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixColored(const style::color &add, int32 w, int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else if (cRetina()) {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	uint64 k = ColoredCacheSkip | (uint64(w) << 32) | uint64(h);
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		QPixmap p(pixColoredNoCache(add, w, h, true));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixBlurredColored(const style::color &add, int32 w, int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else if (cRetina()) {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	uint64 k = BlurredColoredCacheSkip | (uint64(w) << 32) | uint64(h);
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		QPixmap p(pixBlurredColoredNoCache(add, w, h));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixSingle(int32 w, int32 h, int32 outerw, int32 outerh) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else if (cRetina()) {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	uint64 k = 0LL;
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend() || i->width() != (outerw * cIntRetinaFactor()) || i->height() != (outerh * cIntRetinaFactor())) {
		if (i != _sizesCache.cend()) {
			globalAcquiredSize -= int64(i->width()) * i->height() * 4;
		}
		QPixmap p(pixNoCache(w, h, ImagePixSmooth | ImagePixRounded, outerw, outerh));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixBlurredSingle(int w, int h, int32 outerw, int32 outerh) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else if (cRetina()) {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	uint64 k = BlurredCacheSkip | 0LL;
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend() || i->width() != (outerw * cIntRetinaFactor()) || i->height() != (outerh * cIntRetinaFactor())) {
		if (i != _sizesCache.cend()) {
			globalAcquiredSize -= int64(i->width()) * i->height() * 4;
		}
		QPixmap p(pixNoCache(w, h, ImagePixSmooth | ImagePixBlurred | ImagePixRounded, outerw, outerh));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

namespace {
	static inline uint64 _blurGetColors(const uchar *p) {
		return (uint64)p[0] + ((uint64)p[1] << 16) + ((uint64)p[2] << 32) + ((uint64)p[3] << 48);
	}
}

QImage imageBlur(QImage img) {
	QImage::Format fmt = img.format();
	if (fmt != QImage::Format_RGB32 && fmt != QImage::Format_ARGB32_Premultiplied) {
		img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
		t_assert(!img.isNull());
	}

	uchar *pix = img.bits();
	if (pix) {
		int w = img.width(), h = img.height(), wold = w, hold = h;
		const int radius = 3;
		const int r1 = radius + 1;
		const int div = radius * 2 + 1;
		const int stride = w * 4;
		if (radius < 16 && div < w && div < h && stride <= w * 4) {
			bool withalpha = img.hasAlphaChannel();
			if (withalpha) {
				QImage imgsmall(w, h, img.format());
				{
					QPainter p(&imgsmall);
					p.setCompositionMode(QPainter::CompositionMode_Source);
					p.setRenderHint(QPainter::SmoothPixmapTransform);
					p.fillRect(0, 0, w, h, st::transparent->b);
					p.drawImage(QRect(radius, radius, w - 2 * radius, h - 2 * radius), img, QRect(0, 0, w, h));
				}
				QImage was = img;
				img = imgsmall;
				imgsmall = QImage();
				t_assert(!img.isNull());

				pix = img.bits();
				if (!pix) return was;
			}
			uint64 *rgb = new uint64[w * h];

			int x, y, i;

			int yw = 0;
			const int we = w - r1;
			for (y = 0; y < h; y++) {
				uint64 cur = _blurGetColors(&pix[yw]);
				uint64 rgballsum = -radius * cur;
				uint64 rgbsum = cur * ((r1 * (r1 + 1)) >> 1);

				for (i = 1; i <= radius; i++) {
					uint64 cur = _blurGetColors(&pix[yw + i * 4]);
					rgbsum += cur * (r1 - i);
					rgballsum += cur;
				}

				x = 0;

#define update(start, middle, end) \
rgb[y * w + x] = (rgbsum >> 4) & 0x00FF00FF00FF00FFLL; \
rgballsum += _blurGetColors(&pix[yw + (start) * 4]) - 2 * _blurGetColors(&pix[yw + (middle) * 4]) + _blurGetColors(&pix[yw + (end) * 4]); \
rgbsum += rgballsum; \
x++;

				while (x < r1) {
					update(0, x, x + r1);
				}
				while (x < we) {
					update(x - r1, x, x + r1);
				}
				while (x < w) {
					update(x - r1, x, w - 1);
				}

#undef update

				yw += stride;
			}

			const int he = h - r1;
			for (x = 0; x < w; x++) {
				uint64 rgballsum = -radius * rgb[x];
				uint64 rgbsum = rgb[x] * ((r1 * (r1 + 1)) >> 1);
				for (i = 1; i <= radius; i++) {
					rgbsum += rgb[i * w + x] * (r1 - i);
					rgballsum += rgb[i * w + x];
				}

				y = 0;
				int yi = x * 4;

#define update(start, middle, end) \
uint64 res = rgbsum >> 4; \
pix[yi] = res & 0xFF; \
pix[yi + 1] = (res >> 16) & 0xFF; \
pix[yi + 2] = (res >> 32) & 0xFF; \
pix[yi + 3] = (res >> 48) & 0xFF; \
rgballsum += rgb[x + (start) * w] - 2 * rgb[x + (middle) * w] + rgb[x + (end) * w]; \
rgbsum += rgballsum; \
y++; \
yi += stride;

				while (y < r1) {
					update(0, y, y + r1);
				}
				while (y < he) {
					update(y - r1, y, y + r1);
				}
				while (y < h) {
					update(y - r1, y, h - 1);
				}

#undef update
			}

			delete[] rgb;
		}
	}
	return img;
}

const QPixmap &circleMask(int width, int height) {
	t_assert(Global::started());

	uint64 key = uint64(uint32(width)) << 32 | uint64(uint32(height));

	Global::CircleMasksMap &masks(Global::RefCircleMasks());
	auto i = masks.constFind(key);
	if (i == masks.cend()) {
		QImage mask(width, height, QImage::Format_ARGB32_Premultiplied);
		{
			Painter p(&mask);
			p.setRenderHint(QPainter::HighQualityAntialiasing);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.fillRect(0, 0, width, height, st::transparent);
			p.setBrush(st::white);
			p.setPen(Qt::NoPen);
			p.drawEllipse(0, 0, width, height);
		}
		mask.setDevicePixelRatio(cRetinaFactor());
		i = masks.insert(key, QPixmap::fromImage(mask));
	}
	return i.value();
}

void imageCircle(QImage &img) {
	t_assert(!img.isNull());

	img.setDevicePixelRatio(cRetinaFactor());
	img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	t_assert(!img.isNull());

	QPixmap mask = circleMask(img.width(), img.height());
	Painter p(&img);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.drawPixmap(0, 0, mask);
}

void imageRound(QImage &img) {
	t_assert(!img.isNull());

	img.setDevicePixelRatio(cRetinaFactor());
	img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	t_assert(!img.isNull());

	QImage **masks = App::cornersMask();
	int32 w = masks[0]->width(), h = masks[0]->height();
	int32 tw = img.width(), th = img.height();
	if (tw < 2 * w || th < 2 * h) {
		return;
	}

	uchar *bits = img.bits();
	const uchar *c0 = masks[0]->constBits(), *c1 = masks[1]->constBits(), *c2 = masks[2]->constBits(), *c3 = masks[3]->constBits();

	int32 s0 = 0, s1 = (tw - w) * 4, s2 = (th - h) * tw * 4, s3 = ((th - h + 1) * tw - w) * 4;
	for (int32 i = 0; i < w; ++i) {
		for (int32 j = 0; j < h; ++j) {
#define update(s, c) \
		{ \
	uint64 color = _blurGetColors(bits + s + (j * tw + i) * 4); \
	color *= (c[(j * w + i) * 4 + 3] + 1); \
	color = (color >> 8); \
	bits[s + (j * tw + i) * 4] = color & 0xFF; \
	bits[s + (j * tw + i) * 4 + 1] = (color >> 16) & 0xFF; \
	bits[s + (j * tw + i) * 4 + 2] = (color >> 32) & 0xFF; \
	bits[s + (j * tw + i) * 4 + 3] = (color >> 48) & 0xFF; \
		}
			update(s0, c0);
			update(s1, c1);
			update(s2, c2);
			update(s3, c3);
#undef update
		}
	}
}

QImage imageColored(const style::color &add, QImage img) {
	QImage::Format fmt = img.format();
	if (fmt != QImage::Format_RGB32 && fmt != QImage::Format_ARGB32_Premultiplied) {
		img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	}

	uchar *pix = img.bits();
	if (pix) {
		int ca = int(add->c.alphaF() * 0xFF), cr = int(add->c.redF() * 0xFF), cg = int(add->c.greenF() * 0xFF), cb = int(add->c.blueF() * 0xFF);
		const int w = img.width(), h = img.height(), size = w * h * 4;
		for (int32 i = 0; i < size; i += 4) {
			int b = pix[i], g = pix[i + 1], r = pix[i + 2], a = pix[i + 3], aca = a * ca;
			pix[i + 0] = uchar(b + ((aca * (cb - b)) >> 16));
			pix[i + 1] = uchar(g + ((aca * (cg - g)) >> 16));
			pix[i + 2] = uchar(r + ((aca * (cr - r)) >> 16));
			pix[i + 3] = uchar(a + ((aca * (0xFF - a)) >> 16));
		}
	}
	return img;
}

QPixmap imagePix(QImage img, int32 w, int32 h, ImagePixOptions options, int32 outerw, int32 outerh) {
	t_assert(!img.isNull());
	if (options.testFlag(ImagePixBlurred)) {
		img = imageBlur(img);
		t_assert(!img.isNull());
	}
	if (w <= 0 || (w == img.width() && (h <= 0 || h == img.height()))) {
	} else if (h <= 0) {
		img = img.scaledToWidth(w, options.testFlag(ImagePixSmooth) ? Qt::SmoothTransformation : Qt::FastTransformation);
		t_assert(!img.isNull());
	} else {
		img = img.scaled(w, h, Qt::IgnoreAspectRatio, options.testFlag(ImagePixSmooth) ? Qt::SmoothTransformation : Qt::FastTransformation);
		t_assert(!img.isNull());
	}
	if (outerw > 0 && outerh > 0) {
		outerw *= cIntRetinaFactor();
		outerh *= cIntRetinaFactor();
		if (outerw != w || outerh != h) {
			img.setDevicePixelRatio(cRetinaFactor());
			QImage result(outerw, outerh, QImage::Format_ARGB32_Premultiplied);
			result.setDevicePixelRatio(cRetinaFactor());
			{
				QPainter p(&result);
				if (w < outerw || h < outerh) {
					p.fillRect(0, 0, result.width(), result.height(), st::white);
				}
				p.drawImage((result.width() - img.width()) / (2 * cIntRetinaFactor()), (result.height() - img.height()) / (2 * cIntRetinaFactor()), img);
			}
			img = result;
			t_assert(!img.isNull());
		}
	}
	if (options.testFlag(ImagePixCircled)) {
		imageRound(img);
		t_assert(!img.isNull());
	}
	img.setDevicePixelRatio(cRetinaFactor());
	return QPixmap::fromImage(img, Qt::ColorOnly);
}

QPixmap Image::pixNoCache(int w, int h, ImagePixOptions options, int outerw, int outerh) const {
	if (!loading()) const_cast<Image*>(this)->load();
	restore();

	if (_data.isNull()) {
		if (h <= 0 && height() > 0) {
			h = qRound(width() * w / float64(height()));
		}
		return blank()->pixNoCache(w, h, options, outerw, outerh);
	}

	if (isNull() && outerw > 0 && outerh > 0) {
		outerw *= cIntRetinaFactor();
		outerh *= cIntRetinaFactor();

		QImage result(outerw, outerh, QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(cRetinaFactor());

		{
			QPainter p(&result);
			if (w < outerw) {
				p.fillRect(0, 0, (outerw - w) / 2, result.height(), st::white);
				p.fillRect(((outerw - w) / 2) + w, 0, result.width() - (((outerw - w) / 2) + w), result.height(), st::white);
			}
			if (h < outerh) {
				p.fillRect(qMax(0, (outerw - w) / 2), 0, qMin(result.width(), w), (outerh - h) / 2, st::white);
				p.fillRect(qMax(0, (outerw - w) / 2), ((outerh - h) / 2) + h, qMin(result.width(), w), result.height() - (((outerh - h) / 2) + h), st::white);
			}
			p.fillRect(qMax(0, (outerw - w) / 2), qMax(0, (outerh - h) / 2), qMin(result.width(), w), qMin(result.height(), h), st::white);
		}

		if (options.testFlag(ImagePixCircled)) {
			imageCircle(result);
		} else if (options.testFlag(ImagePixRounded)) {
			imageRound(result);
		}
		return QPixmap::fromImage(result, Qt::ColorOnly);
	}

	return imagePix(_data.toImage(), w, h, options, outerw, outerh);
}

QPixmap Image::pixColoredNoCache(const style::color &add, int32 w, int32 h, bool smooth) const {
	const_cast<Image*>(this)->load();
	restore();
	if (_data.isNull()) return blank()->pix();

	QImage img = _data.toImage();
	if (w <= 0 || !width() || !height() || (w == width() && (h <= 0 || h == height()))) return QPixmap::fromImage(imageColored(add, img));
	if (h <= 0) {
		return QPixmap::fromImage(imageColored(add, img.scaledToWidth(w, smooth ? Qt::SmoothTransformation : Qt::FastTransformation)), Qt::ColorOnly);
	}
	return QPixmap::fromImage(imageColored(add, img.scaled(w, h, Qt::IgnoreAspectRatio, smooth ? Qt::SmoothTransformation : Qt::FastTransformation)), Qt::ColorOnly);
}

QPixmap Image::pixBlurredColoredNoCache(const style::color &add, int32 w, int32 h) const {
	const_cast<Image*>(this)->load();
	restore();
	if (_data.isNull()) return blank()->pix();

	QImage img = imageBlur(_data.toImage());
	if (h <= 0) {
		img = img.scaledToWidth(w, Qt::SmoothTransformation);
	} else {
		img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	}

	return QPixmap::fromImage(imageColored(add, img), Qt::ColorOnly);
}

void Image::forget() const {
	if (_forgot) return;

	if (_data.isNull()) return;

	invalidateSizeCache();
	if (_saved.isEmpty()) {
		QBuffer buffer(&_saved);
		if (!_data.save(&buffer, _format)) {
			if (_data.save(&buffer, "PNG")) {
				_format = "PNG";
			} else {
				return;
			}
		}
	}
	globalAcquiredSize -= int64(_data.width()) * _data.height() * 4;
	_data = QPixmap();
	_forgot = true;
}

void Image::restore() const {
	if (!_forgot) return;

	QBuffer buffer(&_saved);
	QImageReader reader(&buffer, _format);
#if QT_VERSION >= QT_VERSION_CHECK(5, 5, 0)
	reader.setAutoTransform(true);
#endif
	_data = QPixmap::fromImageReader(&reader, Qt::ColorOnly);

	if (!_data.isNull()) {
		globalAcquiredSize += int64(_data.width()) * _data.height() * 4;
	}
	_forgot = false;
}

void Image::invalidateSizeCache() const {
	for (Sizes::const_iterator i = _sizesCache.cbegin(), e = _sizesCache.cend(); i != e; ++i) {
		if (!i->isNull()) {
			globalAcquiredSize -= int64(i->width()) * i->height() * 4;
		}
	}
	_sizesCache.clear();
}

Image::~Image() {
	invalidateSizeCache();
	if (!_data.isNull()) {
		globalAcquiredSize -= int64(_data.width()) * _data.height() * 4;
	}
}

void clearStorageImages() {
	for (StorageImages::const_iterator i = storageImages.cbegin(), e = storageImages.cend(); i != e; ++i) {
		delete i.value();
	}
	storageImages.clear();
	for (WebImages::const_iterator i = webImages.cbegin(), e = webImages.cend(); i != e; ++i) {
		delete i.value();
	}
	webImages.clear();
}

void clearAllImages() {
	for (LocalImages::const_iterator i = localImages.cbegin(), e = localImages.cend(); i != e; ++i) {
		delete i.value();
	}
	localImages.clear();
	clearStorageImages();
}

int64 imageCacheSize() {
	return globalAcquiredSize;
}

void RemoteImage::doCheckload() const {
	if (!amLoading() || !_loader->done()) return;

	QPixmap data = _loader->imagePixmap(shrinkBox());
	if (data.isNull()) {
		_loader->deleteLater();
		_loader->stop();
		_loader = CancelledFileLoader;
		return;
	}

	if (!_data.isNull()) {
		globalAcquiredSize -= int64(_data.width()) * _data.height() * 4;
	}

	_format = _loader->imageFormat(shrinkBox());
	_data = data;
	_saved = _loader->bytes();
	const_cast<RemoteImage*>(this)->setInformation(_saved.size(), _data.width(), _data.height());
	globalAcquiredSize += int64(_data.width()) * _data.height() * 4;

	invalidateSizeCache();

	_loader->deleteLater();
	_loader->stop();
	_loader = nullptr;

	_forgot = false;
}

void RemoteImage::loadLocal() {
	if (loaded() || amLoading()) return;

	_loader = createLoader(LoadFromLocalOnly, true);
	if (_loader) _loader->start();
}

void RemoteImage::setData(QByteArray &bytes, const QByteArray &bytesFormat) {
	QBuffer buffer(&bytes);

	if (!_data.isNull()) {
		globalAcquiredSize -= int64(_data.width()) * _data.height() * 4;
	}
	QByteArray fmt(bytesFormat);
	_data = QPixmap::fromImage(App::readImage(bytes, &fmt, false), Qt::ColorOnly);
	if (!_data.isNull()) {
		globalAcquiredSize += int64(_data.width()) * _data.height() * 4;
		setInformation(bytes.size(), _data.width(), _data.height());
	}

	invalidateSizeCache();
	if (amLoading()) {
		_loader->deleteLater();
		_loader->stop();
		_loader = nullptr;
	}
	_saved = bytes;
	_format = fmt;
	_forgot = false;
}

void RemoteImage::automaticLoad(const HistoryItem *item) {
	if (loaded()) return;

	if (_loader != CancelledFileLoader && item) {
		bool loadFromCloud = false;
		if (item->history()->peer->isUser()) {
			loadFromCloud = !(cAutoDownloadPhoto() & dbiadNoPrivate);
		} else {
			loadFromCloud = !(cAutoDownloadPhoto() & dbiadNoGroups);
		}

		if (_loader) {
			if (loadFromCloud) _loader->permitLoadFromCloud();
		} else {
			_loader = createLoader(loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly, true);
			if (_loader) _loader->start();
		}
	}
}

void RemoteImage::automaticLoadSettingsChanged() {
	if (loaded() || _loader != CancelledFileLoader) return;
	_loader = 0;
}

void RemoteImage::load(bool loadFirst, bool prior) {
	if (loaded()) return;

	if (!_loader) {
		_loader = createLoader(LoadFromCloudOrLocal, false);
	}
	if (amLoading()) {
		_loader->start(loadFirst, prior);
	}
}

void RemoteImage::loadEvenCancelled(bool loadFirst, bool prior) {
	if (_loader == CancelledFileLoader) _loader = 0;
	return load(loadFirst, prior);
}

RemoteImage::~RemoteImage() {
	if (!_data.isNull()) {
		globalAcquiredSize -= int64(_data.width()) * _data.height() * 4;
	}
	if (amLoading()) {
		_loader->deleteLater();
		_loader->stop();
		_loader = 0;
	}
}

bool RemoteImage::loaded() const {
	doCheckload();
	return (!_data.isNull() || !_saved.isNull());
}

bool RemoteImage::displayLoading() const {
	return amLoading() && (!_loader->loadingLocal() || !_loader->autoLoading());
}

void RemoteImage::cancel() {
	if (!amLoading()) return;

	FileLoader *l = _loader;
	_loader = CancelledFileLoader;
	if (l) {
		l->cancel();
		l->deleteLater();
		l->stop();
	}
}

float64 RemoteImage::progress() const {
	return amLoading() ? _loader->currentProgress() : (loaded() ? 1 : 0);
}

int32 RemoteImage::loadOffset() const {
	return amLoading() ? _loader->currentOffset() : 0;
}

StorageImage::StorageImage(const StorageImageLocation &location, int32 size)
: _location(location)
, _size(size) {
}

StorageImage::StorageImage(const StorageImageLocation &location, QByteArray &bytes)
: _location(location)
, _size(bytes.size()) {
	setData(bytes);
	if (!_location.isNull()) {
		Local::writeImage(storageKey(_location), StorageImageSaved(mtpToStorageType(mtpc_storage_filePartial), bytes));
	}
}

int32 StorageImage::countWidth() const {
	return _location.width();
}

int32 StorageImage::countHeight() const {
	return _location.height();
}

void StorageImage::setInformation(int32 size, int32 width, int32 height) {
	_size = size;
	_location.setSize(width, height);
}

FileLoader *StorageImage::createLoader(LoadFromCloudSetting fromCloud, bool autoLoading) {
	if (_location.isNull()) return 0;
	return new mtpFileLoader(&_location, _size, fromCloud, autoLoading);
}

DelayedStorageImage::DelayedStorageImage() : StorageImage(StorageImageLocation())
, _loadRequested(false)
, _loadCancelled(false)
, _loadFromCloud(false) {
}

DelayedStorageImage::DelayedStorageImage(int32 w, int32 h) : StorageImage(StorageImageLocation(w, h, 0, 0, 0, 0))
, _loadRequested(false)
, _loadCancelled(false)
, _loadFromCloud(false) {
}

DelayedStorageImage::DelayedStorageImage(QByteArray &bytes) : StorageImage(StorageImageLocation(), bytes)
, _loadRequested(false)
, _loadCancelled(false)
, _loadFromCloud(false) {
}

void DelayedStorageImage::setStorageLocation(const StorageImageLocation location) {
	_location = location;
	if (_loadRequested) {
		if (!_loadCancelled) {
			if (_loadFromCloud) {
				load();
			} else {
				loadLocal();
			}
		}
		_loadRequested = false;
	}
}

void DelayedStorageImage::automaticLoad(const HistoryItem *item) {
	if (_location.isNull()) {
		if (!_loadCancelled && item) {
			bool loadFromCloud = false;
			if (item->history()->peer->isUser()) {
				loadFromCloud = !(cAutoDownloadPhoto() & dbiadNoPrivate);
			} else {
				loadFromCloud = !(cAutoDownloadPhoto() & dbiadNoGroups);
			}

			if (_loadRequested) {
				if (loadFromCloud) _loadFromCloud = loadFromCloud;
			} else {
				_loadFromCloud = loadFromCloud;
				_loadRequested = true;
			}
		}
	} else {
		StorageImage::automaticLoad(item);
	}
}

void DelayedStorageImage::automaticLoadSettingsChanged() {
	if (_loadCancelled) _loadCancelled = false;
	StorageImage::automaticLoadSettingsChanged();
}

void DelayedStorageImage::load(bool loadFirst, bool prior) {
	if (_location.isNull()) {
		_loadRequested = _loadFromCloud = true;
	} else {
		StorageImage::load(loadFirst, prior);
	}
}

void DelayedStorageImage::loadEvenCancelled(bool loadFirst, bool prior) {
	_loadCancelled = false;
	StorageImage::loadEvenCancelled(loadFirst, prior);
}

bool DelayedStorageImage::displayLoading() const {
	return _location.isNull() ? true : StorageImage::displayLoading();
}

void DelayedStorageImage::cancel() {
	if (_loadRequested) {
		_loadRequested = false;
	}
	StorageImage::cancel();
}

WebImage::WebImage(const QString &url, QSize box) : _url(url), _box(box), _size(0), _width(0), _height(0) {
}

WebImage::WebImage(const QString &url, int width, int height) : _url(url), _size(0), _width(width), _height(height) {
}

void WebImage::setSize(int width, int height) {
	_width = width;
	_height = height;
}

int32 WebImage::countWidth() const {
	return _width;
}

int32 WebImage::countHeight() const {
	return _height;
}

void WebImage::setInformation(int32 size, int32 width, int32 height) {
	_size = size;
	setSize(width, height);
}

FileLoader *WebImage::createLoader(LoadFromCloudSetting fromCloud, bool autoLoading) {
	return new webFileLoader(_url, QString(), fromCloud, autoLoading);
}

namespace internal {

Image *getImage(const QString &file, QByteArray format) {
	if (file.startsWith(qstr("http://"), Qt::CaseInsensitive) || file.startsWith(qstr("https://"), Qt::CaseInsensitive)) {
		QString key = file;
		WebImages::const_iterator i = webImages.constFind(key);
		if (i == webImages.cend()) {
			i = webImages.insert(key, new WebImage(file));
		}
		return i.value();
	} else {
		QFileInfo f(file);
		QString key = qsl("//:%1//:%2//:").arg(f.size()).arg(f.lastModified().toTime_t()) + file;
		LocalImages::const_iterator i = localImages.constFind(key);
		if (i == localImages.cend()) {
			i = localImages.insert(key, new Image(file, format));
		}
		return i.value();
	}
}

Image *getImage(const QString &url, QSize box) {
	QString key = qsl("//:%1//:%2//:").arg(box.width()).arg(box.height()) + url;
	auto i = webImages.constFind(key);
	if (i == webImages.cend()) {
		i = webImages.insert(key, new WebImage(url, box));
	}
	return i.value();
}

Image *getImage(const QString &url, int width, int height) {
	QString key = url;
	auto i = webImages.constFind(key);
	if (i == webImages.cend()) {
		i = webImages.insert(key, new WebImage(url, width, height));
	} else {
		i.value()->setSize(width, height);
	}
	return i.value();
}

Image *getImage(const QByteArray &filecontent, QByteArray format) {
	return new Image(filecontent, format);
}

Image *getImage(const QPixmap &pixmap, QByteArray format) {
	return new Image(pixmap, format);
}

Image *getImage(const QByteArray &filecontent, QByteArray format, const QPixmap &pixmap) {
	return new Image(filecontent, format, pixmap);
}

Image *getImage(int32 width, int32 height) {
	return new DelayedStorageImage(width, height);
}

StorageImage *getImage(const StorageImageLocation &location, int32 size) {
	StorageKey key(storageKey(location));
	StorageImages::const_iterator i = storageImages.constFind(key);
	if (i == storageImages.cend()) {
		i = storageImages.insert(key, new StorageImage(location, size));
	}
	return i.value();
}

StorageImage *getImage(const StorageImageLocation &location, const QByteArray &bytes) {
	StorageKey key(storageKey(location));
	StorageImages::const_iterator i = storageImages.constFind(key);
	if (i == storageImages.cend()) {
		QByteArray bytesArr(bytes);
		i = storageImages.insert(key, new StorageImage(location, bytesArr));
	} else if (!i.value()->loaded()) {
		QByteArray bytesArr(bytes);
		i.value()->setData(bytesArr);
		if (!location.isNull()) {
			Local::writeImage(key, StorageImageSaved(mtpToStorageType(mtpc_storage_filePartial), bytes));
		}
	}
	return i.value();
}

} // namespace internal

ReadAccessEnabler::ReadAccessEnabler(const PsFileBookmark *bookmark) : _bookmark(bookmark), _failed(_bookmark ? !_bookmark->enable() : false) {
}

ReadAccessEnabler::ReadAccessEnabler(const QSharedPointer<PsFileBookmark> &bookmark) : _bookmark(bookmark.data()), _failed(_bookmark ? !_bookmark->enable() : false) {
}

ReadAccessEnabler::~ReadAccessEnabler() {
	if (_bookmark && !_failed) _bookmark->disable();
}

FileLocation::FileLocation(StorageFileType type, const QString &name) : type(type), fname(name) {
	if (fname.isEmpty()) {
		size = 0;
		type = StorageFileUnknown;
	} else {
		setBookmark(psPathBookmark(name));

		QFileInfo f(name);
		if (f.exists()) {
			qint64 s = f.size();
			if (s > INT_MAX) {
				fname = QString();
				_bookmark.clear();
				size = 0;
				type = StorageFileUnknown;
			} else {
				modified = f.lastModified();
				size = qint32(s);
			}
		} else {
			fname = QString();
			_bookmark.clear();
			size = 0;
			type = StorageFileUnknown;
		}
	}
}

bool FileLocation::check() const {
	if (fname.isEmpty()) return false;

	ReadAccessEnabler enabler(_bookmark);
	if (enabler.failed()) {
		const_cast<FileLocation*>(this)->_bookmark.clear();
	}

	QFileInfo f(name());
	if (!f.isReadable()) return false;

	quint64 s = f.size();
	if (s > INT_MAX) return false;

	return (f.lastModified() == modified) && (qint32(s) == size);
}

const QString &FileLocation::name() const {
	return _bookmark ? _bookmark->name(fname) : fname;
}

QByteArray FileLocation::bookmark() const {
	return _bookmark ? _bookmark->bookmark() : QByteArray();
}

void FileLocation::setBookmark(const QByteArray &bm) {
	_bookmark.reset(bm.isEmpty() ? nullptr : new PsFileBookmark(bm));
}

bool FileLocation::accessEnable() const {
	return isEmpty() ? false : (_bookmark ? _bookmark->enable() : true);
}

void FileLocation::accessDisable() const {
	return _bookmark ? _bookmark->disable() : (void)0;
}
