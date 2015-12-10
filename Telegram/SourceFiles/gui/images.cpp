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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "gui/images.h"

#include "mainwidget.h"
#include "localstorage.h"

#include "pspecific.h"

namespace {
	typedef QMap<QString, LocalImage*> LocalImages;
	LocalImages localImages;

	Image *blank() {
		static Image *img = getImage(qsl(":/gui/art/blank.gif"), "GIF");
		return img;
	}

	typedef QMap<StorageKey, StorageImage*> StorageImages;
	StorageImages storageImages;

	int64 globalAquiredSize = 0;

	static const uint64 BlurredCacheSkip = 0x1000000000000000LLU;
	static const uint64 ColoredCacheSkip = 0x2000000000000000LLU;
	static const uint64 BlurredColoredCacheSkip = 0x3000000000000000LLU;
	static const uint64 RoundedCacheSkip = 0x4000000000000000LLU;
}

bool Image::isNull() const {
	return (this == blank());
}

ImagePtr::ImagePtr() : Parent(blank()) {
}

ImagePtr::ImagePtr(int32 width, int32 height, const MTPFileLocation &location, ImagePtr def) :
	Parent((location.type() == mtpc_fileLocation) ? (Image*)(getImage(StorageImageLocation(width, height, location.c_fileLocation()))) : def.v()) {
}

const QPixmap &Image::pix(int32 w, int32 h) const {
	restore();
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
		QPixmap p(pixNoCache(w, h, true));
        if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixRounded(int32 w, int32 h) const {
	restore();
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
		QPixmap p(pixNoCache(w, h, true, false, true));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixBlurred(int32 w, int32 h) const {
	restore();
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
		QPixmap p(pixNoCache(w, h, true, true));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixColored(const style::color &add, int32 w, int32 h) const {
	restore();
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
			globalAquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixBlurredColored(const style::color &add, int32 w, int32 h) const {
	restore();
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
			globalAquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixSingle(int32 w, int32 h, int32 outerw, int32 outerh) const {
	restore();
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else if (cRetina()) {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	uint64 k = 0LL;
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend() || i->width() != w || (h && i->height() != h)) {
		if (i != _sizesCache.cend()) {
			globalAquiredSize -= int64(i->width()) * i->height() * 4;
		}
		QPixmap p(pixNoCache(w, h, true, false, true, outerw, outerh));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixBlurredSingle(int32 w, int32 h, int32 outerw, int32 outerh) const {
	restore();
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else if (cRetina()) {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	uint64 k = BlurredCacheSkip | 0LL;
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend() || i->width() != w || (h && i->height() != h)) {
		if (i != _sizesCache.cend()) {
			globalAquiredSize -= int64(i->width()) * i->height() * 4;
		}
		QPixmap p(pixNoCache(w, h, true, true, true, outerw, outerh));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAquiredSize += int64(p.width()) * p.height() * 4;
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

void imageRound(QImage &img) {
	img.setDevicePixelRatio(cRetinaFactor());
	img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);

	QImage **masks = App::cornersMask();
	int32 w = masks[0]->width(), h = masks[0]->height();
	int32 tw = img.width(), th = img.height();

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

QPixmap Image::pixNoCache(int32 w, int32 h, bool smooth, bool blurred, bool rounded, int32 outerw, int32 outerh) const {
	restore();
	loaded();

	const QPixmap &p(pixData());
	if (p.isNull()) return blank()->pix();

	QImage img = p.toImage();
	if (blurred) img = imageBlur(img);
	if (w <= 0 || !width() || !height() || (w == width() && (h <= 0 || h == height()))) {
	} else if (h <= 0) {
		img = img.scaledToWidth(w, smooth ? Qt::SmoothTransformation : Qt::FastTransformation);
	} else {
		img = img.scaled(w, h, Qt::IgnoreAspectRatio, smooth ? Qt::SmoothTransformation : Qt::FastTransformation);
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
					p.fillRect(0, 0, result.width(), result.height(), st::black->b);
				}
				p.drawImage((result.width() - img.width()) / (2 * cIntRetinaFactor()), (result.height() - img.height()) / (2 * cIntRetinaFactor()), img);
			}
			img = result;
		}
	}
	if (rounded) imageRound(img);
	return QPixmap::fromImage(img, Qt::ColorOnly);
}

QPixmap Image::pixColoredNoCache(const style::color &add, int32 w, int32 h, bool smooth) const {
	restore();
	loaded();

	const QPixmap &p(pixData());
	if (p.isNull()) {
		return blank()->pix();
	}
	if (w <= 0 || !width() || !height() || (w == width() && (h <= 0 || h == height()))) return QPixmap::fromImage(imageColored(add, p.toImage()));
	if (h <= 0) {
		return QPixmap::fromImage(imageColored(add, p.toImage().scaledToWidth(w, smooth ? Qt::SmoothTransformation : Qt::FastTransformation)), Qt::ColorOnly);
	}
	return QPixmap::fromImage(imageColored(add, p.toImage().scaled(w, h, Qt::IgnoreAspectRatio, smooth ? Qt::SmoothTransformation : Qt::FastTransformation)), Qt::ColorOnly);
}

QPixmap Image::pixBlurredColoredNoCache(const style::color &add, int32 w, int32 h) const {
	restore();
	loaded();

	const QPixmap &p(pixData());
	if (p.isNull()) return blank()->pix();

	QImage img = imageBlur(p.toImage());
	if (h <= 0) {
		img = img.scaledToWidth(w, Qt::SmoothTransformation);
	} else {
		img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	}

	return QPixmap::fromImage(imageColored(add, img), Qt::ColorOnly);
}

void Image::forget() const {
	if (forgot) return;

	const QPixmap &p(pixData());
	if (p.isNull()) return;

	invalidateSizeCache();
	if (saved.isEmpty()) {
		QBuffer buffer(&saved);
		if (format.toLower() == "webp") {
			int a = 0;
		}
		if (!p.save(&buffer, format)) {
			if (p.save(&buffer, "PNG")) {
				format = "PNG";
			} else {
				return;
			}
		}
	}
	globalAquiredSize -= int64(p.width()) * p.height() * 4;
	doForget();
	forgot = true;
}

void Image::restore() const {
	if (!forgot) return;
	doRestore();
	const QPixmap &p(pixData());
	if (!p.isNull()) {
		globalAquiredSize += int64(p.width()) * p.height() * 4;
	}
	forgot = false;
}

void Image::invalidateSizeCache() const {
	for (Sizes::const_iterator i = _sizesCache.cbegin(), e = _sizesCache.cend(); i != e; ++i) {
		if (!i->isNull()) {
			globalAquiredSize -= int64(i->width()) * i->height() * 4;
		}
	}
	_sizesCache.clear();
}

LocalImage::LocalImage(const QString &file, QByteArray fmt) {
	data = QPixmap::fromImage(App::readImage(file, &fmt, false, 0, &saved), Qt::ColorOnly);
	format = fmt;
	if (!data.isNull()) {
		globalAquiredSize += int64(data.width()) * data.height() * 4;
	}
}

LocalImage::LocalImage(const QByteArray &filecontent, QByteArray fmt) {
	data = QPixmap::fromImage(App::readImage(filecontent, &fmt, false), Qt::ColorOnly);
	format = fmt;
	saved = filecontent;
	if (!data.isNull()) {
		globalAquiredSize += int64(data.width()) * data.height() * 4;
	}
}

LocalImage::LocalImage(const QPixmap &pixmap, QByteArray format) : Image(format), data(pixmap) {
	if (!data.isNull()) {
		globalAquiredSize += int64(data.width()) * data.height() * 4;
	}
}

LocalImage::LocalImage(const QByteArray &filecontent, QByteArray fmt, const QPixmap &pixmap) {
	data = pixmap;
	format = fmt;
	saved = filecontent;
	if (!data.isNull()) {
		globalAquiredSize += int64(data.width()) * data.height() * 4;
	}
}

const QPixmap &LocalImage::pixData() const {
	return data;
}

int32 LocalImage::width() const {
	restore();
	return data.width();
}

int32 LocalImage::height() const {
	restore();
	return data.height();
}

LocalImage::~LocalImage() {
	if (!data.isNull()) {
		globalAquiredSize -= int64(data.width()) * data.height() * 4;
	}
}

LocalImage *getImage(const QString &file, QByteArray format) {
	QFileInfo f(file);
	QString key = qsl("//:%1//:%2//:").arg(f.size()).arg(f.lastModified().toTime_t()) + file;
	LocalImages::const_iterator i = localImages.constFind(key);
	if (i == localImages.cend()) {
		i = localImages.insert(key, new LocalImage(file, format));
	}
	return i.value();
}

LocalImage *getImage(const QByteArray &filecontent, QByteArray format) {
	return new LocalImage(filecontent, format);
}

LocalImage *getImage(const QPixmap &pixmap, QByteArray format) {
	return new LocalImage(pixmap, format);
}

LocalImage *getImage(const QByteArray &filecontent, QByteArray format, const QPixmap &pixmap) {
	return new LocalImage(filecontent, format, pixmap);
}

void clearStorageImages() {
	for (StorageImages::const_iterator i = storageImages.cbegin(), e = storageImages.cend(); i != e; ++i) {
		delete i.value();
	}
	storageImages.clear();
}

void clearAllImages() {
	for (LocalImages::const_iterator i = localImages.cbegin(), e = localImages.cend(); i != e; ++i) {
		delete i.value();
	}
	localImages.clear();
	clearStorageImages();
}

int64 imageCacheSize() {
	return globalAquiredSize;
}

StorageImage::StorageImage(const StorageImageLocation &location, int32 size) : w(location.width), h(location.height), loader(new mtpFileLoader(location.dc, location.volume, location.local, location.secret, size)) {
}

StorageImage::StorageImage(const StorageImageLocation &location, QByteArray &bytes) : w(location.width), h(location.height), loader(0) {
	setData(bytes);
	if (location.dc) {
		Local::writeImage(storageKey(location.dc, location.volume, location.local), StorageImageSaved(mtpToStorageType(mtpc_storage_filePartial), bytes));
	}
}

const QPixmap &StorageImage::pixData() const {
	return data;
}

int32 StorageImage::width() const {
	return w;
}

int32 StorageImage::height() const {
	return h;
}

bool StorageImage::check() const {
	if (loader->done()) {
		if (!data.isNull()) {
			globalAquiredSize -= int64(data.width()) * data.height() * 4;
		}
		format = loader->imageFormat();
		data = loader->imagePixmap();
		QByteArray bytes = loader->bytes();
		if (!data.isNull()) {
			globalAquiredSize += int64(data.width()) * data.height() * 4;
		}

		w = data.width();
		h = data.height();
		invalidateSizeCache();
		loader->deleteLater();
		loader->rpcInvalidate();
		loader = 0;

		saved = bytes;
		forgot = false;
		return true;
	}
	return false;
}

void StorageImage::setData(QByteArray &bytes, const QByteArray &format) {
	QBuffer buffer(&bytes);

	if (!data.isNull()) {
		globalAquiredSize -= int64(data.width()) * data.height() * 4;
	}
	QByteArray fmt(format);
	data = QPixmap::fromImage(App::readImage(bytes, &fmt, false), Qt::ColorOnly);
	if (!data.isNull()) {
		globalAquiredSize += int64(data.width()) * data.height() * 4;
	}

	w = data.width();
	h = data.height();
	invalidateSizeCache();
	if (loader) {
		loader->deleteLater();
		loader->rpcInvalidate();
		loader = 0;
	}
	this->saved = bytes;
	this->format = fmt;
	forgot = false;
}

StorageImage::~StorageImage() {
	if (!data.isNull()) {
		globalAquiredSize -= int64(data.width()) * data.height() * 4;
	}
	if (loader) {
		loader->deleteLater();
		loader->rpcInvalidate();
		loader = 0;
	}
}

bool StorageImage::loaded() const {
	if (!loader) return true;
	return check();
}

StorageImage *getImage(const StorageImageLocation &location, int32 size) {
	StorageKey key(storageKey(location.dc, location.volume, location.local));
	StorageImages::const_iterator i = storageImages.constFind(key);
	if (i == storageImages.cend()) {
		i = storageImages.insert(key, new StorageImage(location, size));
	}
	return i.value();
}

StorageImage *getImage(const StorageImageLocation &location, const QByteArray &bytes) {
	StorageKey key(storageKey(location.dc, location.volume, location.local));
	StorageImages::const_iterator i = storageImages.constFind(key);
    if (i == storageImages.cend()) {
        QByteArray bytesArr(bytes);
        i = storageImages.insert(key, new StorageImage(location, bytesArr));
	} else if (!i.value()->loaded()) {
        QByteArray bytesArr(bytes);
        i.value()->setData(bytesArr);
		if (location.dc) {
			Local::writeImage(storageKey(location.dc, location.volume, location.local), StorageImageSaved(mtpToStorageType(mtpc_storage_filePartial), bytes));
		}
	}
	return i.value();
}

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
				_bookmark.reset(0);
				size = 0;
				type = StorageFileUnknown;
			} else {
				modified = f.lastModified();
				size = qint32(s);
			}
		} else {
			fname = QString();
			_bookmark.reset(0);
			size = 0;
			type = StorageFileUnknown;
		}
	}
}

bool FileLocation::check() const {
	if (fname.isEmpty()) return false;

	ReadAccessEnabler enabler(_bookmark);
	if (enabler.failed()) {
		const_cast<FileLocation*>(this)->_bookmark.reset(0);
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
	if (bm.isEmpty()) {
		_bookmark.reset(0);
	} else {
		_bookmark.reset(new PsFileBookmark(bm));
	}
}

bool FileLocation::accessEnable() const {
	return isEmpty() ? false : (_bookmark ? _bookmark->enable() : true);
}

void FileLocation::accessDisable() const {
	return _bookmark ? _bookmark->disable() : (void)0;
}
