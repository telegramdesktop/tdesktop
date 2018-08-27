/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "data/data_file_origin.h"

namespace Storage {
namespace Cache {
struct Key;
} // namespace Cache
} // namespace Storage

enum class ImageRoundRadius {
	None,
	Large,
	Small,
	Ellipse,
};

namespace Images {

QPixmap PixmapFast(QImage &&image);

QImage prepareBlur(QImage image);
void prepareRound(
	QImage &image,
	ImageRoundRadius radius,
	RectParts corners = RectPart::AllCorners,
	QRect target = QRect());
void prepareRound(
	QImage &image,
	QImage *cornerMasks,
	RectParts corners = RectPart::AllCorners,
	QRect target = QRect());
void prepareCircle(QImage &image);
QImage prepareColored(style::color add, QImage image);
QImage prepareOpaque(QImage image);

enum class Option {
	None                  = 0,
	Smooth                = (1 << 0),
	Blurred               = (1 << 1),
	Circled               = (1 << 2),
	RoundedLarge          = (1 << 3),
	RoundedSmall          = (1 << 4),
	RoundedTopLeft        = (1 << 5),
	RoundedTopRight       = (1 << 6),
	RoundedBottomLeft     = (1 << 7),
	RoundedBottomRight    = (1 << 8),
	RoundedAll            = (None
		| RoundedTopLeft
		| RoundedTopRight
		| RoundedBottomLeft
		| RoundedBottomRight),
	Colored               = (1 << 9),
	TransparentBackground = (1 << 10),
};
using Options = base::flags<Option>;
inline constexpr auto is_flag_type(Option) { return true; };

QImage prepare(QImage img, int w, int h, Options options, int outerw, int outerh, const style::color *colored = nullptr);

inline QPixmap pixmap(QImage img, int w, int h, Options options, int outerw, int outerh, const style::color *colored = nullptr) {
	return QPixmap::fromImage(prepare(img, w, h, options, outerw, outerh, colored), Qt::ColorOnly);
}

} // namespace Images

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

class DelayedStorageImage;

class HistoryItem;
class Image {
public:
	Image(const QString &file, QByteArray format = QByteArray());
	Image(const QByteArray &filecontent, QByteArray format = QByteArray());
	Image(const QPixmap &pixmap, QByteArray format = QByteArray());
	Image(const QByteArray &filecontent, QByteArray format, const QPixmap &pixmap);

	virtual void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	}
	virtual void automaticLoadSettingsChanged() {
	}

	virtual bool loaded() const {
		return true;
	}
	virtual bool loading() const {
		return false;
	}
	virtual bool displayLoading() const {
		return false;
	}
	virtual void cancel() {
	}
	virtual float64 progress() const {
		return 1;
	}
	virtual int32 loadOffset() const {
		return 0;
	}

	const QPixmap &pix(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixRounded(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0,
		ImageRoundRadius radius = ImageRoundRadius::None,
		RectParts corners = RectPart::AllCorners) const;
	const QPixmap &pixBlurred(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixColored(
		Data::FileOrigin origin,
		style::color add,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixBlurredColored(
		Data::FileOrigin origin,
		style::color add,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixSingle(
		Data::FileOrigin origin,
		int32 w,
		int32 h,
		int32 outerw,
		int32 outerh,
		ImageRoundRadius radius,
		RectParts corners = RectPart::AllCorners,
		const style::color *colored = nullptr) const;
	const QPixmap &pixBlurredSingle(
		Data::FileOrigin origin,
		int32 w,
		int32 h,
		int32 outerw,
		int32 outerh,
		ImageRoundRadius radius,
		RectParts corners = RectPart::AllCorners) const;
	const QPixmap &pixCircled(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	const QPixmap &pixBlurredCircled(
		Data::FileOrigin origin,
		int32 w = 0,
		int32 h = 0) const;
	QPixmap pixNoCache(
		Data::FileOrigin origin,
		int w = 0,
		int h = 0,
		Images::Options options = 0,
		int outerw = -1,
		int outerh = -1,
		const style::color *colored = nullptr) const;
	QPixmap pixColoredNoCache(
		Data::FileOrigin origin,
		style::color add,
		int32 w = 0,
		int32 h = 0,
		bool smooth = false) const;
	QPixmap pixBlurredColoredNoCache(
		Data::FileOrigin origin,
		style::color add,
		int32 w,
		int32 h = 0) const;

	int32 width() const {
		return qMax(countWidth(), 1);
	}

	int32 height() const {
		return qMax(countHeight(), 1);
	}

	virtual void load(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) {
	}

	virtual void loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) {
	}

	virtual const StorageImageLocation &location() const {
		return StorageImageLocation::Null;
	}
	virtual base::optional<Storage::Cache::Key> cacheKey() const;

	bool isNull() const;

	void forget() const;

	QByteArray savedFormat() const {
		return _format;
	}
	QByteArray savedData() const {
		return _saved;
	}

	virtual DelayedStorageImage *toDelayedStorageImage() {
		return 0;
	}
	virtual const DelayedStorageImage *toDelayedStorageImage() const {
		return 0;
	}

	virtual ~Image();

protected:
	Image(QByteArray format = "PNG") : _format(format), _forgot(false) {
	}

	void restore() const;
	virtual void checkload() const {
	}
	void invalidateSizeCache() const;

	virtual int32 countWidth() const {
		restore();
		return _data.width();
	}

	virtual int32 countHeight() const {
		restore();
		return _data.height();
	}

	mutable QByteArray _saved, _format;
	mutable bool _forgot;
	mutable QPixmap _data;

private:
	using Sizes = QMap<uint64, QPixmap>;
	mutable Sizes _sizesCache;

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

class RemoteImage : public Image {
public:
	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) override; // auto load photo
	void automaticLoadSettingsChanged() override;

	bool loaded() const override;
	bool loading() const override {
		return amLoading();
	}
	bool displayLoading() const override;
	void cancel() override;
	float64 progress() const override;
	int32 loadOffset() const override;

	void setImageBytes(
		const QByteArray &bytes,
		const QByteArray &format = QByteArray());

	void load(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) override;
	void loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) override;

	~RemoteImage();

protected:
	// If after loading the image we need to shrink it to fit into a
	// specific size, you can return this size here.
	virtual QSize shrinkBox() const {
		return QSize();
	}
	virtual void setInformation(int32 size, int32 width, int32 height) = 0;
	virtual FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) = 0;

	void checkload() const override {
		doCheckload();
	}
	void loadLocal();

private:
	mutable FileLoader *_loader = nullptr;
	bool amLoading() const;
	void doCheckload() const;

	void destroyLoaderDelayed(FileLoader *newValue = nullptr) const;

};

class StorageImage : public RemoteImage {
public:
	explicit StorageImage(const StorageImageLocation &location, int32 size = 0);
	StorageImage(const StorageImageLocation &location, const QByteArray &bytes);

	const StorageImageLocation &location() const override {
		return _location;
	}
	base::optional<Storage::Cache::Key> cacheKey() const override;
	void refreshFileReference(const QByteArray &data) {
		_location.refreshFileReference(data);
	}

protected:
	void setInformation(int32 size, int32 width, int32 height) override;
	FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) override;

	int32 countWidth() const override;
	int32 countHeight() const override;

	StorageImageLocation _location;
	int32 _size;

};

class WebFileImage : public RemoteImage {
public:
	WebFileImage(const WebFileLocation &location, QSize box, int size = 0);
	WebFileImage(
		const WebFileLocation &location,
		int width,
		int height,
		int size = 0);

	base::optional<Storage::Cache::Key> cacheKey() const override;

protected:
	void setInformation(int size, int width, int height) override;
	FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) override;

	QSize shrinkBox() const override {
		return _box;
	}

	int countWidth() const override;
	int countHeight() const override;

	WebFileLocation _location;
	QSize _box;
	int _width = 0;
	int _height = 0;
	int _size = 0;

};

class DelayedStorageImage : public StorageImage {
public:
	DelayedStorageImage();
	DelayedStorageImage(int32 w, int32 h);
	//DelayedStorageImage(QByteArray &bytes);

	void setStorageLocation(
		Data::FileOrigin origin,
		const StorageImageLocation location);

	virtual DelayedStorageImage *toDelayedStorageImage() override {
		return this;
	}
	virtual const DelayedStorageImage *toDelayedStorageImage() const override {
		return this;
	}

	void automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) override; // auto load photo
	void automaticLoadSettingsChanged() override;

	bool loading() const override {
		return _location.isNull() ? _loadRequested : StorageImage::loading();
	}
	bool displayLoading() const override;
	void cancel() override;

	void load(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) override;
	void loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst = false,
		bool prior = true) override;

private:
	bool _loadRequested, _loadCancelled, _loadFromCloud;

};

class WebImage : public RemoteImage {
public:
	// If !box.isEmpty() then resize the image to fit in this box.
	WebImage(const QString &url, QSize box = QSize());
	WebImage(const QString &url, int width, int height);

	void setSize(int width, int height);

	base::optional<Storage::Cache::Key> cacheKey() const override;

protected:
	QSize shrinkBox() const override {
		return _box;
	}
	void setInformation(int32 size, int32 width, int32 height) override;
	FileLoader *createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) override;

	int32 countWidth() const override;
	int32 countHeight() const override;

private:
	QString _url;
	QSize _box;
	int32 _size, _width, _height;

};

namespace internal {

Image *getImage(const QString &file, QByteArray format);
Image *getImage(const QString &url, QSize box);
Image *getImage(const QString &url, int width, int height);
Image *getImage(const QByteArray &filecontent, QByteArray format);
Image *getImage(const QPixmap &pixmap, QByteArray format);
Image *getImage(
	const QByteArray &filecontent,
	QByteArray format,
	const QPixmap &pixmap);
Image *getImage(int32 width, int32 height);
StorageImage *getImage(const StorageImageLocation &location, int size = 0);
StorageImage *getImage( // photoCachedSize
	const StorageImageLocation &location,
	const QByteArray &bytes);
Image *getImage(const MTPWebDocument &location);
Image *getImage(const MTPWebDocument &location, QSize box);
WebFileImage *getImage(
	const WebFileLocation &location,
	int width,
	int height,
	int size = 0);
WebFileImage *getImage(
	const WebFileLocation &location,
	QSize box,
	int size = 0);

} // namespace internal

class ImagePtr : public ManagedPtr<Image> {
public:
	ImagePtr();
	ImagePtr(const QString &file, QByteArray format = QByteArray()) : Parent(internal::getImage(file, format)) {
	}
	ImagePtr(const QString &url, QSize box) : Parent(internal::getImage(url, box)) {
	}
	ImagePtr(const QString &url, int width, int height) : Parent(internal::getImage(url, width, height)) {
	}
	ImagePtr(const QByteArray &filecontent, QByteArray format = QByteArray()) : Parent(internal::getImage(filecontent, format)) {
	}
	ImagePtr(const QByteArray &filecontent, QByteArray format, const QPixmap &pixmap) : Parent(internal::getImage(filecontent, format, pixmap)) {
	}
	ImagePtr(const QPixmap &pixmap, QByteArray format) : Parent(internal::getImage(pixmap, format)) {
	}
	ImagePtr(const StorageImageLocation &location, int32 size = 0) : Parent(internal::getImage(location, size)) {
	}
	ImagePtr(const StorageImageLocation &location, const QByteArray &bytes) : Parent(internal::getImage(location, bytes)) {
	}
	ImagePtr(const MTPWebDocument &location) : Parent(internal::getImage(location)) {
	}
	ImagePtr(const MTPWebDocument &location, QSize box) : Parent(internal::getImage(location, box)) {
	}
	ImagePtr(const WebFileLocation &location, int width, int height, int size = 0)
	: Parent(internal::getImage(location, width, height, size)) {
	}
	ImagePtr(const WebFileLocation &location, QSize box, int size = 0)
		: Parent(internal::getImage(location, box, size)) {
	}
	ImagePtr(int32 width, int32 height, const MTPFileLocation &location, ImagePtr def = ImagePtr());
	ImagePtr(int32 width, int32 height) : Parent(internal::getImage(width, height)) {
	}

	explicit operator bool() const {
		return (_data != nullptr) && !_data->isNull();
	}

};

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

void clearStorageImages();
void clearAllImages();
int64 imageCacheSize();

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
