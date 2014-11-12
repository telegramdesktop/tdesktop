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
#include "gui/images.h"

#include "mainwidget.h"

namespace {
	typedef QMap<QString, LocalImage*> LocalImages;
	LocalImages localImages;

	Image *blank() {
		static Image *img = getImage(qsl(":/gui/art/blank.gif"));
		return img;
	}

	typedef QMap<QByteArray, StorageImage*> StorageImages;
	StorageImages storageImages;

	QByteArray storageKey(int32 dc, const int64 &volume, int32 local, const int64 &secret) {
		QByteArray result(24, Qt::Uninitialized);
		memcpy(result.data(), &dc, 4);
		memcpy(result.data() + 4, &volume, 8);
		memcpy(result.data() + 12, &local, 4);
		memcpy(result.data() + 16, &secret, 8);
		return result;
	}

	int64 globalAquiredSize = 0;
}

bool Image::isNull() const {
	return (this == blank());
}

ImagePtr::ImagePtr() : Parent(blank()) {
}

ImagePtr::ImagePtr(int32 width, int32 height, const MTPFileLocation &location, ImagePtr def) :
	Parent((location.type() == mtpc_fileLocation) ? (Image*)(getImage(width, height, location.c_fileLocation().vdc_id.v, location.c_fileLocation().vvolume_id.v, location.c_fileLocation().vlocal_id.v, location.c_fileLocation().vsecret.v)) : def.v()) {
}

const QPixmap &Image::pix(int32 w, int32 h) const {
	restore();
	checkload();

	if (w <= 0 || !width() || !height()) {
        w = width() * cIntRetinaFactor();
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

const QPixmap &Image::pixBlurred(int32 w, int32 h) const {
	restore();
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else if (cRetina()) {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	uint64 k = 0x8000000000000000LL | (uint64(w) << 32) | uint64(h);
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		QPixmap p(pixBlurredNoCache(w, h));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixSingle(int32 w, int32 h) const {
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
	if (i == _sizesCache.cend() || i->width() != w || h && i->height() != h) {
		if (i != _sizesCache.cend()) {
			globalAquiredSize -= int64(i->width()) * i->height() * 4;
		}
		QPixmap p(pixNoCache(w, h, true));
		if (cRetina()) p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			globalAquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixBlurredSingle(int32 w, int32 h) const {
	restore();
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else if (cRetina()) {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	uint64 k = 0x8000000000000000LL | 0LL;
	Sizes::const_iterator i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend() || i->width() != w || h && i->height() != h) {
		if (i != _sizesCache.cend()) {
			globalAquiredSize -= int64(i->width()) * i->height() * 4;
		}
		QPixmap p(pixBlurredNoCache(w, h));
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
		return p[0] + (p[1] << 16) + ((uint64)p[2] << 32);
	}
}

QImage imageBlur(QImage img) {
	QImage::Format fmt = img.format();
	if (fmt != QImage::Format_RGB32 && fmt != QImage::Format_ARGB32 && fmt != QImage::Format_ARGB32_Premultiplied) {
		QImage tmp(img.width(), img.height(), QImage::Format_ARGB32);
		{
			QPainter p(&tmp);
			p.drawImage(0, 0, img);
		}
		img = tmp;
	}

	uchar *pix = img.bits();
	if (pix) {
		const int w = img.width(), h = img.height(), stride = w * 4;
		const int radius = 7;
		const int r1 = radius + 1;
		const int div = radius * 2 + 1;

		if (radius < 16 && div < w && div < h && stride <= w * 4) {
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
rgb[y * w + x] = (rgbsum >> 6) & 0x00FF00FF00FF00FFLL; \
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
uint64 res = rgbsum >> 6; \
pix[yi] = res & 0xFF; \
pix[yi + 1] = (res >> 16) & 0xFF; \
pix[yi + 2] = (res >> 32) & 0xFF; \
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

QPixmap Image::pixBlurredNoCache(int32 w, int32 h) const {
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

	return QPixmap::fromImage(img);
}

QPixmap Image::pixNoCache(int32 w, int32 h, bool smooth) const {
	restore();
	loaded();

	const QPixmap &p(pixData());
	if (p.isNull()) return blank()->pix();

	if (w <= 0 || !width() || !height() || w == width()) return p;
	if (h <= 0) {
		return QPixmap::fromImage(p.toImage().scaledToWidth(w, smooth ? Qt::SmoothTransformation : Qt::FastTransformation));
	}
	return QPixmap::fromImage(p.toImage().scaled(w, h, Qt::IgnoreAspectRatio, smooth ? Qt::SmoothTransformation : Qt::FastTransformation));
}

void Image::forget() const {
	if (forgot) return;

	const QPixmap &p(pixData());
	if (p.isNull()) return;

	invalidateSizeCache();
	if (saved.isEmpty()) {
		QBuffer buffer(&saved);
		p.save(&buffer, format);
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

LocalImage::LocalImage(const QString &file) : data(file) {
	if (!data.isNull()) {
		globalAquiredSize += int64(data.width()) * data.height() * 4;
	}
}

LocalImage::LocalImage(const QPixmap &pixmap, QByteArray format) : Image(format), data(pixmap) {
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

LocalImage *getImage(const QString &file) {
	LocalImages::const_iterator i = localImages.constFind(file);
	if (i == localImages.cend()) {
		i = localImages.insert(file, new LocalImage(file));
	}
	return i.value();
}

LocalImage::~LocalImage() {
	if (!data.isNull()) {
		globalAquiredSize -= int64(data.width()) * data.height() * 4;
	}
}

LocalImage *getImage(const QPixmap &pixmap, QByteArray format) {
	return new LocalImage(pixmap, format);
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

StorageImage::StorageImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, int32 size) : w(width), h(height), loader(new mtpFileLoader(dc, volume, local, secret, size)) {
}

StorageImage::StorageImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, QByteArray &bytes) : w(width), h(height), loader(0) {
	setData(bytes);
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
		switch (loader->fileType()) {
		case mtpc_storage_fileGif: format = "GIF"; break;
		case mtpc_storage_fileJpeg: format = "JPG"; break;
		case mtpc_storage_filePng: format = "PNG"; break;
		default: format = QByteArray(); break;
		}
		if (!data.isNull()) {
			globalAquiredSize -= int64(data.width()) * data.height() * 4;
		}
		QByteArray bytes = loader->bytes();
		data = QPixmap::fromImage(App::readImage(bytes, &format), Qt::ColorOnly);
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

	QImageReader reader(&buffer, format);
	if (!data.isNull()) {
		globalAquiredSize -= int64(data.width()) * data.height() * 4;
	}
	data = QPixmap::fromImageReader(&reader, Qt::ColorOnly);
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
	this->format = reader.format();
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

StorageImage *getImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, int32 size) {
	QByteArray key(storageKey(dc, volume, local, secret));
	StorageImages::const_iterator i = storageImages.constFind(key);
	if (i == storageImages.cend()) {
		i = storageImages.insert(key, new StorageImage(width, height, dc, volume, local, secret, size));
	}
	return i.value();
}

StorageImage *getImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, const QByteArray &bytes) {
	QByteArray key(storageKey(dc, volume, local, secret));
	StorageImages::const_iterator i = storageImages.constFind(key);
    if (i == storageImages.cend()) {
        QByteArray bytesArr(bytes);
        i = storageImages.insert(key, new StorageImage(width, height, dc, volume, local, secret, bytesArr));
	} else if (!i.value()->loaded()) {
        QByteArray bytesArr(bytes);
        i.value()->setData(bytesArr);
	}
	return i.value();
}
