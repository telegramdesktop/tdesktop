/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/image_location.h"

#include "ui/image/image.h"
#include "platform/platform_specific.h"

ImagePtr::ImagePtr() : _data(Image::Blank().get()) {
}

ImagePtr::ImagePtr(not_null<Image*> data) : _data(data) {
}

Image *ImagePtr::operator->() const {
	return _data;
}
Image *ImagePtr::get() const {
	return _data;
}

ImagePtr::operator bool() const {
	return !_data->isNull();
}

StorageImageLocation StorageImageLocation::Null;
WebFileLocation WebFileLocation::Null;

StorageImageLocation::StorageImageLocation(
	int32 width,
	int32 height,
	int32 dc,
	const uint64 &volume,
	int32 local,
	const uint64 &secret,
	const QByteArray &fileReference)
: _widthheight(packIntInt(width, height))
, _dclocal(packIntInt(dc, local))
, _volume(volume)
, _secret(secret)
, _fileReference(fileReference) {
}

StorageImageLocation::StorageImageLocation(
	int32 width,
	int32 height,
	const MTPDfileLocation &location)
: StorageImageLocation(
	width,
	height,
	location.vdc_id.v,
	location.vvolume_id.v,
	location.vlocal_id.v,
	location.vsecret.v,
	location.vfile_reference.v) {
}

ReadAccessEnabler::ReadAccessEnabler(const PsFileBookmark *bookmark)
: _bookmark(bookmark)
, _failed(_bookmark ? !_bookmark->enable() : false) {
}

ReadAccessEnabler::ReadAccessEnabler(
	const std::shared_ptr<PsFileBookmark> &bookmark)
: _bookmark(bookmark.get())
, _failed(_bookmark ? !_bookmark->enable() : false) {
}

ReadAccessEnabler::~ReadAccessEnabler() {
	if (_bookmark && !_failed) _bookmark->disable();
}

FileLocation::FileLocation(const QString &name) : fname(name) {
	if (fname.isEmpty()) {
		size = 0;
	} else {
		setBookmark(psPathBookmark(name));

		QFileInfo f(name);
		if (f.exists()) {
			qint64 s = f.size();
			if (s > INT_MAX) {
				fname = QString();
				_bookmark = nullptr;
				size = 0;
			} else {
				modified = f.lastModified();
				size = qint32(s);
			}
		} else {
			fname = QString();
			_bookmark = nullptr;
			size = 0;
		}
	}
}

bool FileLocation::check() const {
	if (fname.isEmpty()) return false;

	ReadAccessEnabler enabler(_bookmark);
	if (enabler.failed()) {
		const_cast<FileLocation*>(this)->_bookmark = nullptr;
	}

	QFileInfo f(name());
	if (!f.isReadable()) return false;

	quint64 s = f.size();
	if (s > INT_MAX) {
		DEBUG_LOG(("File location check: Wrong size %1").arg(s));
		return false;
	}

	if (qint32(s) != size) {
		DEBUG_LOG(("File location check: Wrong size %1 when should be %2").arg(s).arg(size));
		return false;
	}
	auto realModified = f.lastModified();
	if (realModified != modified) {
		DEBUG_LOG(("File location check: Wrong last modified time %1 when should be %2").arg(realModified.toMSecsSinceEpoch()).arg(modified.toMSecsSinceEpoch()));
		return false;
	}
	return true;
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
