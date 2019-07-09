/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class FileLoader;

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

namespace Data {
struct UpdatedFileReferences;
} // namespace Data

enum LoadFromCloudSetting {
	LoadFromCloudOrLocal,
	LoadFromLocalOnly,
};

enum LoadToCacheSetting {
	LoadToFileOnly,
	LoadToCacheAsWell,
};

using InMemoryKey = std::pair<uint64, uint64>;

namespace std {

template<>
struct hash<InMemoryKey> {
	size_t operator()(InMemoryKey value) const {
		auto seed = hash<uint64>()(value.first);
		seed ^= hash<uint64>()(value.second)
			+ std::size_t(0x9e3779b9)
			+ (seed << 6) + (seed >> 2);
		return seed;
	}

};

} // namespace std

class StorageFileLocation {
public:
	// Those are used in serialization, don't change.
	enum class Type : uint8 {
		Legacy          = 0x00,
		Encrypted       = 0x01,
		Document        = 0x02,
		Secure          = 0x03,
		Takeout         = 0x04,
		Photo           = 0x05,
		PeerPhoto       = 0x06,
		StickerSetThumb = 0x07,
	};

	StorageFileLocation() = default;
	StorageFileLocation(
		int32 dcId,
		int32 self,
		const MTPInputFileLocation &tl);

	[[nodiscard]] StorageFileLocation convertToModern(
		Type type,
		uint64 id,
		uint64 accessHash) const;

	[[nodiscard]] int32 dcId() const;
	[[nodiscard]] uint64 objectId() const;
	[[nodiscard]] MTPInputFileLocation tl(int32 self) const;

	[[nodiscard]] QByteArray serialize() const;
	[[nodiscard]] int serializeSize() const;
	[[nodiscard]] static std::optional<StorageFileLocation> FromSerialized(
		const QByteArray &serialized);

	[[nodiscard]] Type type() const;
	[[nodiscard]] bool valid() const;
	[[nodiscard]] Storage::Cache::Key cacheKey() const;
	[[nodiscard]] Storage::Cache::Key bigFileBaseCacheKey() const;

	// We have to allow checking this because of a serialization bug.
	[[nodiscard]] bool isDocumentThumbnail() const;

	[[nodiscard]] QByteArray fileReference() const;
	bool refreshFileReference(const Data::UpdatedFileReferences &updates);
	bool refreshFileReference(const QByteArray &data);

	[[nodiscard]] static const StorageFileLocation &Invalid();

private:
	friend bool operator==(
		const StorageFileLocation &a,
		const StorageFileLocation &b);
	friend bool operator<(
		const StorageFileLocation &a,
		const StorageFileLocation &b);

	uint16 _dcId = 0;
	Type _type = Type::Legacy;
	uint8 _sizeLetter = 0;
	int32 _localId = 0;
	uint64 _id = 0;
	uint64 _accessHash = 0;
	uint64 _volumeId = 0;
	uint32 _inMessagePeerId = 0; // > 0 'userId', < 0 '-channelId'.
	uint32 _inMessageId = 0;
	QByteArray _fileReference;

};

inline bool operator!=(
		const StorageFileLocation &a,
		const StorageFileLocation &b) {
	return !(a == b);
}

inline bool operator>(
		const StorageFileLocation &a,
		const StorageFileLocation &b) {
	return (b < a);
}

inline bool operator<=(
		const StorageFileLocation &a,
		const StorageFileLocation &b) {
	return !(b < a);
}

inline bool operator>=(
		const StorageFileLocation &a,
		const StorageFileLocation &b) {
	return !(a < b);
}

class StorageImageLocation {
public:
	StorageImageLocation() = default;
	StorageImageLocation(
		const StorageFileLocation &file,
		int width,
		int height);

	[[nodiscard]] QByteArray serialize() const;
	[[nodiscard]] int serializeSize() const;
	[[nodiscard]] static std::optional<StorageImageLocation> FromSerialized(
		const QByteArray &serialized);

	[[nodiscard]] StorageImageLocation convertToModern(
			StorageFileLocation::Type type,
			uint64 id,
			uint64 accessHash) const {
		return StorageImageLocation(
			_file.convertToModern(type, id, accessHash),
			_width,
			_height);
	}

	[[nodiscard]] const StorageFileLocation &file() const {
		return _file;
	}
	[[nodiscard]] int width() const {
		return _width;
	}
	[[nodiscard]] int height() const {
		return _height;
	}

	void setSize(int width, int height) {
		_width = width;
		_height = height;
	}

	[[nodiscard]] StorageFileLocation::Type type() const {
		return _file.type();
	}
	[[nodiscard]] bool valid() const {
		return _file.valid();
	}
	[[nodiscard]] QByteArray fileReference() const {
		return _file.fileReference();
	}
	bool refreshFileReference(const QByteArray &data) {
		return _file.refreshFileReference(data);
	}
	bool refreshFileReference(const Data::UpdatedFileReferences &updates) {
		return _file.refreshFileReference(updates);
	}

	[[nodiscard]] static const StorageImageLocation &Invalid() {
		static auto result = StorageImageLocation();
		return result;
	}

private:
	friend inline bool operator==(
			const StorageImageLocation &a,
			const StorageImageLocation &b) {
		return (a._file == b._file);
	}
	friend inline bool operator<(
			const StorageImageLocation &a,
			const StorageImageLocation &b) {
		return (a._file < b._file);
	}

	StorageFileLocation _file;
	int _width = 0;
	int _height = 0;

};

inline bool operator!=(
		const StorageImageLocation &a,
		const StorageImageLocation &b) {
	return !(a == b);
}

inline bool operator>(
		const StorageImageLocation &a,
		const StorageImageLocation &b) {
	return (b < a);
}

inline bool operator<=(
		const StorageImageLocation &a,
		const StorageImageLocation &b) {
	return !(b < a);
}

inline bool operator>=(
		const StorageImageLocation &a,
		const StorageImageLocation &b) {
	return !(a < b);
}

class WebFileLocation {
public:
	WebFileLocation() = default;
	WebFileLocation(const QByteArray &url, uint64 accessHash)
	: _accessHash(accessHash)
	, _url(url) {
	}
	bool isNull() const {
		return _url.isEmpty();
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

	friend inline bool operator==(
			const WebFileLocation &a,
			const WebFileLocation &b) {
		return (a._accessHash == b._accessHash)
			&& (a._url == b._url);
	}
	friend inline bool operator<(
			const WebFileLocation &a,
			const WebFileLocation &b) {
		return std::tie(a._accessHash, a._url)
			< std::tie(b._accessHash, b._url);
	}

};

inline bool operator!=(const WebFileLocation &a, const WebFileLocation &b) {
	return !(a == b);
}

inline bool operator>(const WebFileLocation &a, const WebFileLocation &b) {
	return (b < a);
}

inline bool operator<=(const WebFileLocation &a, const WebFileLocation &b) {
	return !(b < a);
}

inline bool operator>=(const WebFileLocation &a, const WebFileLocation &b) {
	return !(a < b);
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

inline bool operator<(
		const GeoPointLocation &a,
		const GeoPointLocation &b) {
	return std::tie(
		a.access,
		a.lat,
		a.lon,
		a.width,
		a.height,
		a.zoom,
		a.scale)
		< std::tie(
			b.access,
			b.lat,
			b.lon,
			b.width,
			b.height,
			b.zoom,
			b.scale);
}

inline bool operator!=(
		const GeoPointLocation &a,
		const GeoPointLocation &b) {
	return !(a == b);
}

inline bool operator>(
		const GeoPointLocation &a,
		const GeoPointLocation &b) {
	return (b < a);
}

inline bool operator<=(
		const GeoPointLocation &a,
		const GeoPointLocation &b) {
	return !(b < a);
}

inline bool operator>=(
		const GeoPointLocation &a,
		const GeoPointLocation &b) {
	return !(a < b);
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

InMemoryKey inMemoryKey(const StorageFileLocation &location);

inline InMemoryKey inMemoryKey(const StorageImageLocation &location) {
	return inMemoryKey(location.file());
}

inline InMemoryKey inMemoryKey(const WebFileLocation &location) {
	auto result = InMemoryKey();
	const auto &url = location.url();
	const auto sha = hashSha1(url.data(), url.size());
	bytes::copy(
		bytes::object_as_span(&result),
		bytes::make_span(sha).subspan(0, sizeof(result)));
	return result;
}

inline InMemoryKey inMemoryKey(const GeoPointLocation &location) {
	return InMemoryKey(
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

	static FileLocation InMediaCacheLocation();

	[[nodiscard]] bool check() const;
	[[nodiscard]] const QString &name() const;
	void setBookmark(const QByteArray &bookmark);
	QByteArray bookmark() const;
	[[nodiscard]] bool isEmpty() const {
		return name().isEmpty();
	}
	[[nodiscard]] bool inMediaCache() const;

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
