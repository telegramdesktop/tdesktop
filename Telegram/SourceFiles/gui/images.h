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
#pragma once

#include <QtGui/QPixmap>

class Image {
public:

	Image(QByteArray format = "PNG") : format(format), forgot(false) {
	}
	virtual bool loaded() const {
		return true;
	}
	const QPixmap &pix(int32 w = 0, int32 h = 0) const;
	QPixmap pixNoCache(int32 w = 0, int32 h = 0, bool smooth = false) const;

	virtual int32 width() const = 0;
	virtual int32 height() const = 0;

	virtual void load(bool /*loadFirst*/ = false, bool /*prior*/ = true) {
	}

	virtual void checkload() const {
	}

	bool isNull() const;
	
	void forget() const;
	void restore() const;

	virtual ~Image() {
		invalidateSizeCache();
	}

protected:

	virtual const QPixmap &pixData() const = 0;
	virtual void doForget() const = 0;
	virtual void doRestore() const = 0;

	void invalidateSizeCache() const;

	mutable QByteArray saved, format;
	mutable bool forgot;

private:

	typedef QMap<uint64, QPixmap> Sizes;
	mutable Sizes _sizesCache;

};

class LocalImage : public Image {
public:

	LocalImage(const QString &file);
	LocalImage(const QPixmap &pixmap, QByteArray format);
	
	int32 width() const;
	int32 height() const;

	~LocalImage();

protected:

	const QPixmap &pixData() const;
	void doForget() const {
		data = QPixmap();
	}
	void doRestore() const { 
		QBuffer buffer(&saved);
		QImageReader reader(&buffer, format);
		data = QPixmap::fromImageReader(&reader, Qt::ColorOnly);
	}

private:

	mutable QPixmap data;
};

LocalImage *getImage(const QString &file);
LocalImage *getImage(const QPixmap &pixmap, QByteArray format);

class StorageImage : public Image {
public:

	StorageImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret);
	StorageImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, QByteArray &bytes);
	
	int32 width() const;
	int32 height() const;
	bool loaded() const;
	void setData(QByteArray &bytes, const QByteArray &format = "JPG");

	void load(bool loadFirst = false, bool prior = true) {
		if (loader) {
			loader->start(loadFirst, prior);
			check();
		}
	}
	void checkload() const {
		if (loader) {
			if (!loader->loading()) {
				loader->start(true);
			}
			check();
		}
	}

	~StorageImage();

protected:

	const QPixmap &pixData() const;
	bool check() const;
	void doForget() const {
		data = QPixmap();
	}
	void doRestore() const { 
		QBuffer buffer(&saved);
		QImageReader reader(&buffer, format);
		data = QPixmap::fromImageReader(&reader, Qt::ColorOnly);
	}

private:

	mutable QPixmap data;
	mutable int32 w, h;
	mutable mtpFileLoader *loader;
};

StorageImage *getImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret);
StorageImage *getImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, const QByteArray &bytes);
Image *getImage(int32 width, int32 height, const MTPFileLocation &location);

class ImagePtr : public ManagedPtr<Image> {
public:
	ImagePtr();
	ImagePtr(const QString &file) : Parent(getImage(file)) {
	}
	ImagePtr(const QPixmap &pixmap, QByteArray format) : Parent(getImage(pixmap, format)) {
	}
	ImagePtr(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret) : Parent(getImage(width, height, dc, volume, local, secret)) {
	}
	ImagePtr(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, const QByteArray &bytes) : Parent(getImage(width, height, dc, volume, local, secret, bytes)) {
	}
	ImagePtr(int32 width, int32 height, const MTPFileLocation &location, ImagePtr def = ImagePtr());
};

void clearStorageImages();
void clearAllImages();
int64 imageCacheSize();
