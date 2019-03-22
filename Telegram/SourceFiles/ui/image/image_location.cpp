/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/image_location.h"

#include "ui/image/image.h"
#include "platform/platform_specific.h"

ImagePtr::ImagePtr() : _data(Image::Empty()) {
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

WebFileLocation WebFileLocation::Null;

bool StorageFileLocation::valid() const {
	switch (_type) {
	case Type::General:
		return (_dcId != 0) && (_volumeId != 0) && (_localId != 0);

	case Type::Encrypted:
	case Type::Secure:
	case Type::Document:
		return (_dcId != 0) && (_id != 0);

	case Type::Photo:
		return (_dcId != 0) && (_id != 0) && (_sizeLetter != 0);

	case Type::Takeout:
		return true;

	case Type::PeerPhoto:
	case Type::StickerSetThumb:
		return (_dcId != 0) && (_id != 0);
	}
	return false;
}

InMemoryKey StorageFileLocation::inMemoryKey() const {
	switch (_type) {
	case Type::General:
	case Type::PeerPhoto:
	case Type::StickerSetThumb:
		return InMemoryKey(
			(uint64(_type) << 56) | (uint64(_dcId) << 40) | uint32(_localId),
			_volumeId);

	case Type::Encrypted:
	case Type::Secure:
		return InMemoryKey(
			(uint64(_type) << 56) | (uint64(_dcId) << 40),
			_id);

	case Type::Document:
	case Type::Photo:
		return InMemoryKey(
			(uint64(_type) << 56) | (uint64(_dcId) << 40) | _sizeLetter,
			_id);

	case Type::Takeout:
		return InMemoryKey(
			(uint64(_type) << 56),
			0);
	}
	return InMemoryKey();
}

bool operator==(const StorageFileLocation &a, const StorageFileLocation &b) {
	const auto valid = a.valid();
	if (valid != b.valid()) {
		return false;
	} else if (!valid) {
		return true;
	}
	const auto type = a._type;
	if (type != b._type) {
		return false;
	}

	using Type = StorageFileLocation::Type;
	switch (type) {
	case Type::General:
		return (a._dcId == b._dcId)
			&& (a._volumeId == b._volumeId)
			&& (a._localId == b._localId);

	case Type::Encrypted:
	case Type::Secure:
		return (a._dcId == b._dcId) && (a._id == b._id);

	case Type::Photo:
	case Type::Document:
		return (a._dcId == b._dcId)
			&& (a._id == b._id)
			&& (a._sizeLetter == b._sizeLetter);

	case Type::Takeout:
		return true;

	case Type::PeerPhoto:
		return (a._dcId == b._dcId)
			&& (a._volumeId == b._volumeId)
			&& (a._localId == b._localId)
			&& (a._id == b._id)
			&& (a._sizeLetter == b._sizeLetter);

	case Type::StickerSetThumb:
		return (a._dcId == b._dcId)
			&& (a._volumeId == b._volumeId)
			&& (a._localId == b._localId)
			&& (a._id == b._id);
	};
	Unexpected("Type in StorageFileLocation::operator==.");
}

StorageImageLocation::StorageImageLocation(
	const StorageFileLocation &file,
	int width,
	int height)
: _file(file)
, _width(width)
, _height(height) {
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
