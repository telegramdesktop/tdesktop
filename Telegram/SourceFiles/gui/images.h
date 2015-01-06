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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

#include <QtGui/QPixmap>

QImage imageBlur(QImage img);

class Image {
public:

	Image(QByteArray format = "PNG") : format(format), forgot(false) {
	}
	virtual bool loaded() const {
		return true;
	}
	virtual bool loading() const {
		return false;
	}
	const QPixmap &pix(int32 w = 0, int32 h = 0) const;
	const QPixmap &pixBlurred(int32 w = 0, int32 h = 0) const;
	const QPixmap &pixColored(const style::color &add, int32 w = 0, int32 h = 0) const;
	const QPixmap &pixBlurredColored(const style::color &add, int32 w = 0, int32 h = 0) const;
	const QPixmap &pixSingle(int32 w = 0, int32 h = 0) const;
	const QPixmap &pixBlurredSingle(int32 w = 0, int32 h = 0) const;
	QPixmap pixNoCache(int32 w = 0, int32 h = 0, bool smooth = false) const;
	QPixmap pixBlurredNoCache(int32 w, int32 h = 0) const;
	QPixmap pixColoredNoCache(const style::color &add, int32 w = 0, int32 h = 0, bool smooth = false) const;
	QPixmap pixBlurredColoredNoCache(const style::color &add, int32 w, int32 h = 0) const;

	virtual int32 width() const = 0;
	virtual int32 height() const = 0;

	virtual void load(bool /*loadFirst*/ = false, bool /*prior*/ = true) {
	}

	virtual void checkload() const {
	}

	bool isNull() const;
	
	void forget() const;
	void restore() const;

	QByteArray savedFormat() const {
		return format;
	}
	QByteArray savedData() const {
		return saved;
	}

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

	LocalImage(const QString &file, QByteArray format = QByteArray());
	LocalImage(const QByteArray &filecontent, QByteArray format = QByteArray());
	LocalImage(const QPixmap &pixmap, QByteArray format = QByteArray());
	
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

LocalImage *getImage(const QString &file, QByteArray format);
LocalImage *getImage(const QByteArray &filecontent, QByteArray format);
LocalImage *getImage(const QPixmap &pixmap, QByteArray format);

typedef QPair<uint64, uint64> StorageKey;
inline uint64 storageMix32To64(int32 a, int32 b) {
	return (uint64(*reinterpret_cast<uint32*>(&a)) << 32) | uint64(*reinterpret_cast<uint32*>(&b));
}
inline StorageKey storageKey(int32 dc, const int64 &volume, int32 local) {
	return StorageKey(storageMix32To64(dc, local), volume);
}
inline StorageKey storageKey(const MTPDfileLocation &location) {
	return storageKey(location.vdc_id.v, location.vvolume_id.v, location.vlocal_id.v);
}
struct StorageImageSaved {
	StorageImageSaved() : type(mtpc_storage_fileUnknown) {
	}
	StorageImageSaved(mtpTypeId type, const QByteArray &data) : type(type), data(data) {
	}
	mtpTypeId type;
	QByteArray data;
};
class StorageImage : public Image {
public:

	StorageImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, int32 size = 0);
	StorageImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, QByteArray &bytes);
	
	int32 width() const;
	int32 height() const;
	bool loaded() const;
	bool loading() const {
		return loader ? loader->loading() : false;
	}
	void setData(QByteArray &bytes, const QByteArray &format = QByteArray());

	void load(bool loadFirst = false, bool prior = true) {
		if (loader) {
			loader->start(loadFirst, prior);
			if (loader) check();
		}
	}
	void checkload() const {
		if (loader) {
			if (!loader->loading()) {
				loader->start(true);
			}
			if (loader) check();
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

StorageImage *getImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, int32 size = 0);
StorageImage *getImage(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, const QByteArray &bytes);
Image *getImage(int32 width, int32 height, const MTPFileLocation &location);

class ImagePtr : public ManagedPtr<Image> {
public:
	ImagePtr();
	ImagePtr(const QString &file, QByteArray format = QByteArray()) : Parent(getImage(file, format)) {
	}
	ImagePtr(const QByteArray &filecontent, QByteArray format = QByteArray()) : Parent(getImage(filecontent, format)) {
	}
	ImagePtr(const QPixmap &pixmap, QByteArray format) : Parent(getImage(pixmap, format)) {
	}
	ImagePtr(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, int32 size = 0) : Parent(getImage(width, height, dc, volume, local, secret, size)) {
	}
	ImagePtr(int32 width, int32 height, int32 dc, const int64 &volume, int32 local, const int64 &secret, const QByteArray &bytes) : Parent(getImage(width, height, dc, volume, local, secret, bytes)) {
	}
	ImagePtr(int32 width, int32 height, const MTPFileLocation &location, ImagePtr def = ImagePtr());
};

void clearStorageImages();
void clearAllImages();
int64 imageCacheSize();

struct FileLocation {
	FileLocation(mtpTypeId type, const QString &name, const QDateTime &modified, qint32 size) : type(type), name(name), modified(modified), size(size) {
	}
	FileLocation(mtpTypeId type, const QString &name) : type(type), name(name) {
		QFileInfo f(name);
		if (f.exists()) {
			qint64 s = f.size();
			if (s > INT_MAX) {
				this->name = QString();
				size = 0;
				type = mtpc_storage_fileUnknown;
			} else {
				modified = f.lastModified();
				size = qint32(s);
			}
		} else {
			this->name = QString();
			size = 0;
			type = mtpc_storage_fileUnknown;
		}
	}
	FileLocation() : size(0) {
	}
	bool check() const {
		if (name.isEmpty()) return false;
		QFileInfo f(name);
		if (!f.exists()) return false;

		quint64 s = f.size();
		if (s > INT_MAX) return false;

		return (f.lastModified() == modified) && (qint32(s) == size);
	}
	mtpTypeId type;
	QString name;
	QDateTime modified;
	qint32 size;
};
inline bool operator==(const FileLocation &a, const FileLocation &b) {
	return a.type == b.type && a.name == b.name && a.modified == b.modified && a.size == b.size;
}
inline bool operator!=(const FileLocation &a, const FileLocation &b) {
	return !(a == b);
}

typedef QPair<uint64, uint64> MediaKey;
inline uint64 mediaMix32To64(mtpTypeId a, int32 b) {
	return (uint64(*reinterpret_cast<uint32*>(&a)) << 32) | uint64(*reinterpret_cast<uint32*>(&b));
}
inline MediaKey mediaKey(mtpTypeId type, int32 dc, const int64 &id) {
	return MediaKey(mediaMix32To64(type, dc), id);
}
inline StorageKey mediaKey(const MTPDfileLocation &location) {
	return storageKey(location.vdc_id.v, location.vvolume_id.v, location.vlocal_id.v);
}
