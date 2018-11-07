/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class FileLoader;

enum LoadFromCloudSetting {
	LoadFromCloudOrLocal,
	LoadFromLocalOnly,
};

enum LoadToCacheSetting {
	LoadToFileOnly,
	LoadToCacheAsWell,
};

inline uint32 packInt(int32 a) {
	return (a < 0) ? uint32(int64(a) + 0x100000000LL) : uint32(a);
}
inline int32 unpackInt(uint32 a) {
	return (a > 0x7FFFFFFFU) ? int32(int64(a) - 0x100000000LL) : int32(a);
}
inline uint64 packUIntUInt(uint32 a, uint32 b) {
	return (uint64(a) << 32) | uint64(b);
}
inline uint64 packUIntInt(uint32 a, int32 b) {
	return packUIntUInt(a, packInt(b));
}
inline uint64 packIntUInt(int32 a, uint32 b) {
	return packUIntUInt(packInt(a), b);
}
inline uint64 packIntInt(int32 a, int32 b) {
	return packUIntUInt(packInt(a), packInt(b));
}
inline uint32 unpackUIntFirst(uint64 v) {
	return uint32(v >> 32);
}
inline int32 unpackIntFirst(uint64 v) {
	return unpackInt(unpackUIntFirst(v));
}
inline uint32 unpackUIntSecond(uint64 v) {
	return uint32(v & 0xFFFFFFFFULL);
}
inline int32 unpackIntSecond(uint64 v) {
	return unpackInt(unpackUIntSecond(v));
}

class StorageImageLocation {
public:
	StorageImageLocation() = default;
	StorageImageLocation(
		int32 width,
		int32 height,
		int32 dc,
		const uint64 &volume,
		int32 local,
		const uint64 &secret,
		const QByteArray &fileReference);
	StorageImageLocation(
		int32 width,
		int32 height,
		const MTPDfileLocation &location);

	bool isNull() const {
		return !_dclocal;
	}
	int32 width() const {
		return unpackIntFirst(_widthheight);
	}
	int32 height() const {
		return unpackIntSecond(_widthheight);
	}
	void setSize(int32 width, int32 height) {
		_widthheight = packIntInt(width, height);
	}
	int32 dc() const {
		return unpackIntFirst(_dclocal);
	}
	uint64 volume() const {
		return _volume;
	}
	int32 local() const {
		return unpackIntSecond(_dclocal);
	}
	uint64 secret() const {
		return _secret;
	}
	QByteArray fileReference() const {
		return _fileReference;
	}
	void refreshFileReference(const QByteArray &data) {
		if (!data.isEmpty()) {
			_fileReference = data;
		}
	}

	static StorageImageLocation FromMTP(
		int32 width,
		int32 height,
		const MTPFileLocation &location) {
		if (location.type() == mtpc_fileLocation) {
			const auto &data = location.c_fileLocation();
			return StorageImageLocation(width, height, data);
		}
		return StorageImageLocation(width, height, 0, 0, 0, 0, {});
	}
	static StorageImageLocation FromMTP(const MTPPhotoSize &size) {
		switch (size.type()) {
		case mtpc_photoSize: {
			const auto &data = size.c_photoSize();
			return FromMTP(data.vw.v, data.vh.v, data.vlocation);
		} break;
		case mtpc_photoCachedSize: {
			const auto &data = size.c_photoCachedSize();
			return FromMTP(data.vw.v, data.vh.v, data.vlocation);
		} break;
		}
		return StorageImageLocation();
	}

	static StorageImageLocation Null;

private:
	uint64 _widthheight = 0;
	uint64 _dclocal = 0;
	uint64 _volume = 0;
	uint64 _secret = 0;
	QByteArray _fileReference;

	friend inline bool operator==(
			const StorageImageLocation &a,
			const StorageImageLocation &b) {
		return (a._dclocal == b._dclocal) && (a._volume == b._volume);
	}

};

inline bool operator!=(const StorageImageLocation &a, const StorageImageLocation &b) {
	return !(a == b);
}

class WebFileLocation {
public:
	WebFileLocation() = default;
	WebFileLocation(int32 dc, const QByteArray &url, uint64 accessHash)
	: _accessHash(accessHash)
	, _url(url)
	, _dc(dc) {
	}
	bool isNull() const {
		return !_dc;
	}
	int32 dc() const {
		return _dc;
	}
	uint64 accessHash() const {
		return _accessHash;
	}
	const QByteArray &url() const {
		return _url;
	}

	static WebFileLocation Null;

private:
	uint64 _accessHash = 0;
	QByteArray _url;
	int32 _dc = 0;

	friend inline bool operator==(
			const WebFileLocation &a,
			const WebFileLocation &b) {
		return (a._dc == b._dc)
			&& (a._accessHash == b._accessHash)
			&& (a._url == b._url);
	}

};

inline bool operator!=(const WebFileLocation &a, const WebFileLocation &b) {
	return !(a == b);
}

struct GeoPointLocation {
	float64 lat = 0.;
	float64 lon = 0.;
	uint64 access = 0;
	int32 width = 0;
	int32 height = 0;
	int32 zoom = 0;
	int32 scale = 0;
};

inline bool operator==(
		const GeoPointLocation &a,
		const GeoPointLocation &b) {
	return (a.lat == b.lat)
		&& (a.lon == b.lon)
		&& (a.access == b.access)
		&& (a.width == b.width)
		&& (a.height == b.height)
		&& (a.zoom == b.zoom)
		&& (a.scale == b.scale);
}

inline bool operator!=(
		const GeoPointLocation &a,
		const GeoPointLocation &b) {
	return !(a == b);
}

class Image;
class ImagePtr {
public:
	ImagePtr();
	explicit ImagePtr(not_null<Image*> data);

	Image *operator->() const;
	Image *get() const;

	explicit operator bool() const;

private:
	not_null<Image*> _data;

};

typedef QPair<uint64, uint64> StorageKey;
inline uint64 storageMix32To64(int32 a, int32 b) {
	return (uint64(*reinterpret_cast<uint32*>(&a)) << 32) | uint64(*reinterpret_cast<uint32*>(&b));
}
inline StorageKey storageKey(int32 dc, const uint64 &volume, int32 local) {
	return StorageKey(storageMix32To64(dc, local), volume);
}
inline StorageKey storageKey(const MTPDfileLocation &location) {
	return storageKey(location.vdc_id.v, location.vvolume_id.v, location.vlocal_id.v);
}
inline StorageKey storageKey(const StorageImageLocation &location) {
	return storageKey(location.dc(), location.volume(), location.local());
}
inline StorageKey storageKey(const WebFileLocation &location) {
	auto url = location.url();
	auto sha = hashSha1(url.data(), url.size());
	return storageKey(
		location.dc(),
		*reinterpret_cast<const uint64*>(sha.data()),
		*reinterpret_cast<const int32*>(sha.data() + sizeof(uint64)));
}
inline StorageKey storageKey(const GeoPointLocation &location) {
	return StorageKey(
		(uint64(std::round(std::abs(location.lat + 360.) * 1000000)) << 32)
		| uint64(std::round(std::abs(location.lon + 360.) * 1000000)),
		(uint64(location.width) << 32) | uint64(location.height));
}

inline QSize shrinkToKeepAspect(int32 width, int32 height, int32 towidth, int32 toheight) {
	int32 w = qMax(width, 1), h = qMax(height, 1);
	if (w * toheight > h * towidth) {
		h = qRound(h * towidth / float64(w));
		w = towidth;
	} else {
		w = qRound(w * toheight / float64(h));
		h = toheight;
	}
	return QSize(qMax(w, 1), qMax(h, 1));
}

class PsFileBookmark;
class ReadAccessEnabler {
public:
	ReadAccessEnabler(const PsFileBookmark *bookmark);
	ReadAccessEnabler(const std::shared_ptr<PsFileBookmark> &bookmark);
	bool failed() const {
		return _failed;
	}
	~ReadAccessEnabler();

private:
	const PsFileBookmark *_bookmark;
	bool _failed;

};

class FileLocation {
public:
	FileLocation() = default;
	explicit FileLocation(const QString &name);

	bool check() const;
	const QString &name() const;
	void setBookmark(const QByteArray &bookmark);
	QByteArray bookmark() const;
	bool isEmpty() const {
		return name().isEmpty();
	}

	bool accessEnable() const;
	void accessDisable() const;

	QString fname;
	QDateTime modified;
	qint32 size;

private:
	std::shared_ptr<PsFileBookmark> _bookmark;

};
inline bool operator==(const FileLocation &a, const FileLocation &b) {
	return (a.name() == b.name()) && (a.modified == b.modified) && (a.size == b.size);
}
inline bool operator!=(const FileLocation &a, const FileLocation &b) {
	return !(a == b);
}
