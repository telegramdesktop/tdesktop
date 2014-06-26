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

StorageImage::StorageImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret) : w(width), h(height), loader(new mtpFileLoader(dc, volume, local, secret)) {
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
		loader = 0;
	}
}

bool StorageImage::loaded() const {
	if (!loader) return true;
	return check();
}

StorageImage *getImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret) {
	QByteArray key(storageKey(dc, volume, local, secret));
	StorageImages::const_iterator i = storageImages.constFind(key);
	if (i == storageImages.cend()) {
		i = storageImages.insert(key, new StorageImage(width, height, dc, volume, local, secret));
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
