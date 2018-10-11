/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image.h"

#include "storage/file_download.h"
#include "data/data_session.h"
#include "storage/cache/storage_cache_database.h"
#include "history/history_item.h"
#include "history/history.h"
#include "auth_session.h"

namespace {

using LocalImages = QMap<QString, Image*>;
LocalImages localImages;

using WebImages = QMap<QString, WebImage*>;
WebImages webImages;

using StorageImages = QMap<StorageKey, StorageImage*>;
StorageImages storageImages;

using WebFileImages = QMap<StorageKey, WebFileImage*>;
WebFileImages webFileImages;

using GeoPointImages = QMap<StorageKey, GeoPointImage*>;
GeoPointImages geoPointImages;

int64 GlobalAcquiredSize = 0;
int64 LocalAcquiredSize = 0;

uint64 PixKey(int width, int height, Images::Options options) {
	return static_cast<uint64>(width) | (static_cast<uint64>(height) << 24) | (static_cast<uint64>(options) << 48);
}

uint64 SinglePixKey(Images::Options options) {
	return PixKey(0, 0, options);
}

} // namespace

namespace Images {

void ClearRemote() {
	for (auto image : base::take(storageImages)) {
		delete image;
	}
	for (auto image : base::take(webImages)) {
		delete image;
	}
	for (auto image : base::take(webFileImages)) {
		delete image;
	}
	for (auto image : base::take(geoPointImages)) {
		delete image;
	}

	LocalAcquiredSize = GlobalAcquiredSize;
}

void ClearAll() {
	for (auto image : base::take(localImages)) {
		delete image;
	}
	ClearRemote();
}

void CheckCacheSize() {
	const auto now = GlobalAcquiredSize;
	if (GlobalAcquiredSize > LocalAcquiredSize + MemoryForImageCache) {
		Auth().data().forgetMedia();
		LocalAcquiredSize = GlobalAcquiredSize;
	}
}

} // namespace Images

Image::Image(const QString &file, QByteArray fmt) {
	_data = App::pixmapFromImageInPlace(App::readImage(file, &fmt, false, 0, &_saved));
	_format = fmt;
	if (!_data.isNull()) {
		GlobalAcquiredSize += int64(_data.width()) * _data.height() * 4;
	}
}

Image::Image(const QByteArray &filecontent, QByteArray fmt) {
	_data = App::pixmapFromImageInPlace(App::readImage(filecontent, &fmt, false));
	_format = fmt;
	_saved = filecontent;
	if (!_data.isNull()) {
		GlobalAcquiredSize += int64(_data.width()) * _data.height() * 4;
	}
}

Image::Image(const QPixmap &pixmap, QByteArray format) : _format(format), _data(pixmap) {
	if (!_data.isNull()) {
		GlobalAcquiredSize += int64(_data.width()) * _data.height() * 4;
	}
}

Image::Image(const QByteArray &filecontent, QByteArray fmt, const QPixmap &pixmap) : _saved(filecontent), _format(fmt), _data(pixmap) {
	_data = pixmap;
	_format = fmt;
	_saved = filecontent;
	if (!_data.isNull()) {
		GlobalAcquiredSize += int64(_data.width()) * _data.height() * 4;
	}
}

Image *Image::Blank() {
	static const auto blankImage = [] {
		const auto factor = cIntRetinaFactor();
		auto data = QImage(
			factor,
			factor,
			QImage::Format_ARGB32_Premultiplied);
		data.fill(Qt::transparent);
		data.setDevicePixelRatio(cRetinaFactor());
		return Images::details::Create(
			App::pixmapFromImageInPlace(std::move(data)),
			"GIF");
	}();
	return blankImage;
}

bool Image::isNull() const {
	return (this == Blank());
}

const QPixmap &Image::pix(
		Data::FileOrigin origin,
		int32 w,
		int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
        w = width();
    } else {
        w *= cIntRetinaFactor();
        h *= cIntRetinaFactor();
    }
	auto options = Images::Option::Smooth | Images::Option::None;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
        p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixRounded(
		Data::FileOrigin origin,
		int32 w,
		int32 h,
		ImageRoundRadius radius,
		RectParts corners) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Images::Option::Smooth | Images::Option::None;
	auto cornerOptions = [](RectParts corners) {
		return (corners & RectPart::TopLeft ? Images::Option::RoundedTopLeft : Images::Option::None)
			| (corners & RectPart::TopRight ? Images::Option::RoundedTopRight : Images::Option::None)
			| (corners & RectPart::BottomLeft ? Images::Option::RoundedBottomLeft : Images::Option::None)
			| (corners & RectPart::BottomRight ? Images::Option::RoundedBottomRight : Images::Option::None);
	};
	if (radius == ImageRoundRadius::Large) {
		options |= Images::Option::RoundedLarge | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Small) {
		options |= Images::Option::RoundedSmall | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Ellipse) {
		options |= Images::Option::Circled | cornerOptions(corners);
	}
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixCircled(
		Data::FileOrigin origin,
		int32 w,
		int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Images::Option::Smooth | Images::Option::Circled;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixBlurredCircled(
		Data::FileOrigin origin,
		int32 w,
		int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Images::Option::Smooth | Images::Option::Circled | Images::Option::Blurred;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixBlurred(
		Data::FileOrigin origin,
		int32 w,
		int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Images::Option::Smooth | Images::Option::Blurred;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixColored(
		Data::FileOrigin origin,
		style::color add,
		int32 w,
		int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Images::Option::Smooth | Images::Option::Colored;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixColoredNoCache(origin, add, w, h, true);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixBlurredColored(
		Data::FileOrigin origin,
		style::color add,
		int32 w,
		int32 h) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Images::Option::Blurred | Images::Option::Smooth | Images::Option::Colored;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixBlurredColoredNoCache(origin, add, w, h);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixSingle(
		Data::FileOrigin origin,
		int32 w,
		int32 h,
		int32 outerw,
		int32 outerh,
		ImageRoundRadius radius,
		RectParts corners,
		const style::color *colored) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}

	auto options = Images::Option::Smooth | Images::Option::None;
	auto cornerOptions = [](RectParts corners) {
		return (corners & RectPart::TopLeft ? Images::Option::RoundedTopLeft : Images::Option::None)
			| (corners & RectPart::TopRight ? Images::Option::RoundedTopRight : Images::Option::None)
			| (corners & RectPart::BottomLeft ? Images::Option::RoundedBottomLeft : Images::Option::None)
			| (corners & RectPart::BottomRight ? Images::Option::RoundedBottomRight : Images::Option::None);
	};
	if (radius == ImageRoundRadius::Large) {
		options |= Images::Option::RoundedLarge | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Small) {
		options |= Images::Option::RoundedSmall | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Ellipse) {
		options |= Images::Option::Circled | cornerOptions(corners);
	}
	if (colored) {
		options |= Images::Option::Colored;
	}

	auto k = SinglePixKey(options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend() || i->width() != (outerw * cIntRetinaFactor()) || i->height() != (outerh * cIntRetinaFactor())) {
		if (i != _sizesCache.cend()) {
			GlobalAcquiredSize -= int64(i->width()) * i->height() * 4;
		}
		auto p = pixNoCache(origin, w, h, options, outerw, outerh, colored);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

const QPixmap &Image::pixBlurredSingle(
		Data::FileOrigin origin,
		int w,
		int h,
		int32 outerw,
		int32 outerh,
		ImageRoundRadius radius,
		RectParts corners) const {
	checkload();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}

	auto options = Images::Option::Smooth | Images::Option::Blurred;
	auto cornerOptions = [](RectParts corners) {
		return (corners & RectPart::TopLeft ? Images::Option::RoundedTopLeft : Images::Option::None)
			| (corners & RectPart::TopRight ? Images::Option::RoundedTopRight : Images::Option::None)
			| (corners & RectPart::BottomLeft ? Images::Option::RoundedBottomLeft : Images::Option::None)
			| (corners & RectPart::BottomRight ? Images::Option::RoundedBottomRight : Images::Option::None);
	};
	if (radius == ImageRoundRadius::Large) {
		options |= Images::Option::RoundedLarge | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Small) {
		options |= Images::Option::RoundedSmall | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Ellipse) {
		options |= Images::Option::Circled | cornerOptions(corners);
	}

	auto k = SinglePixKey(options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend() || i->width() != (outerw * cIntRetinaFactor()) || i->height() != (outerh * cIntRetinaFactor())) {
		if (i != _sizesCache.cend()) {
			GlobalAcquiredSize -= int64(i->width()) * i->height() * 4;
		}
		auto p = pixNoCache(origin, w, h, options, outerw, outerh);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
	}
	return i.value();
}

QPixmap Image::pixNoCache(
		Data::FileOrigin origin,
		int w,
		int h,
		Images::Options options,
		int outerw,
		int outerh,
		const style::color *colored) const {
	if (!loading()) const_cast<Image*>(this)->load(origin);
	restore();

	if (_data.isNull()) {
		if (h <= 0 && height() > 0) {
			h = qRound(width() * w / float64(height()));
		}
		return Blank()->pixNoCache(origin, w, h, options, outerw, outerh);
	}

	if (isNull() && outerw > 0 && outerh > 0) {
		outerw *= cIntRetinaFactor();
		outerh *= cIntRetinaFactor();

		QImage result(outerw, outerh, QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(cRetinaFactor());

		{
			QPainter p(&result);
			if (w < outerw) {
				p.fillRect(0, 0, (outerw - w) / 2, result.height(), st::imageBg);
				p.fillRect(((outerw - w) / 2) + w, 0, result.width() - (((outerw - w) / 2) + w), result.height(), st::imageBg);
			}
			if (h < outerh) {
				p.fillRect(qMax(0, (outerw - w) / 2), 0, qMin(result.width(), w), (outerh - h) / 2, st::imageBg);
				p.fillRect(qMax(0, (outerw - w) / 2), ((outerh - h) / 2) + h, qMin(result.width(), w), result.height() - (((outerh - h) / 2) + h), st::imageBg);
			}
			p.fillRect(qMax(0, (outerw - w) / 2), qMax(0, (outerh - h) / 2), qMin(result.width(), w), qMin(result.height(), h), st::imageBgTransparent);
		}

		auto corners = [](Images::Options options) {
			return ((options & Images::Option::RoundedTopLeft) ? RectPart::TopLeft : RectPart::None)
				| ((options & Images::Option::RoundedTopRight) ? RectPart::TopRight : RectPart::None)
				| ((options & Images::Option::RoundedBottomLeft) ? RectPart::BottomLeft : RectPart::None)
				| ((options & Images::Option::RoundedBottomRight) ? RectPart::BottomRight : RectPart::None);
		};
		if (options & Images::Option::Circled) {
			Images::prepareCircle(result);
		} else if (options & Images::Option::RoundedLarge) {
			Images::prepareRound(result, ImageRoundRadius::Large, corners(options));
		} else if (options & Images::Option::RoundedSmall) {
			Images::prepareRound(result, ImageRoundRadius::Small, corners(options));
		}
		if (options & Images::Option::Colored) {
			Assert(colored != nullptr);
			result = Images::prepareColored(*colored, std::move(result));
		}
		return App::pixmapFromImageInPlace(std::move(result));
	}

	return Images::pixmap(_data.toImage(), w, h, options, outerw, outerh, colored);
}

QPixmap Image::pixColoredNoCache(
		Data::FileOrigin origin,
		style::color add,
		int32 w,
		int32 h,
		bool smooth) const {
	const_cast<Image*>(this)->load(origin);
	restore();
	if (_data.isNull()) {
		return Blank()->pix(origin);
	}

	auto img = _data.toImage();
	if (w <= 0 || !width() || !height() || (w == width() && (h <= 0 || h == height()))) {
		return App::pixmapFromImageInPlace(Images::prepareColored(add, std::move(img)));
	}
	if (h <= 0) {
		return App::pixmapFromImageInPlace(Images::prepareColored(add, img.scaledToWidth(w, smooth ? Qt::SmoothTransformation : Qt::FastTransformation)));
	}
	return App::pixmapFromImageInPlace(Images::prepareColored(add, img.scaled(w, h, Qt::IgnoreAspectRatio, smooth ? Qt::SmoothTransformation : Qt::FastTransformation)));
}

QPixmap Image::pixBlurredColoredNoCache(
		Data::FileOrigin origin,
		style::color add,
		int32 w,
		int32 h) const {
	const_cast<Image*>(this)->load(origin);
	restore();
	if (_data.isNull()) {
		return Blank()->pix(origin);
	}

	auto img = Images::prepareBlur(_data.toImage());
	if (h <= 0) {
		img = img.scaledToWidth(w, Qt::SmoothTransformation);
	} else {
		img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	}

	return App::pixmapFromImageInPlace(Images::prepareColored(add, img));
}

void Image::forget() const {
	if (_forgot) return;

	checkload();
	if (_data.isNull()) return;

	invalidateSizeCache();
	/*if (hasLocalCopy()) {
		_saved.clear();
	} else */if (_saved.isEmpty()) {
		QBuffer buffer(&_saved);
		if (!_data.save(&buffer, _format)) {
			if (_data.save(&buffer, "PNG")) {
				_format = "PNG";
			} else {
				return;
			}
		}
	}
	GlobalAcquiredSize -= int64(_data.width()) * _data.height() * 4;
	_data = QPixmap();
	_forgot = true;
}

void Image::restore() const {
	if (!_forgot) return;

	QBuffer buffer(&_saved);
	QImageReader reader(&buffer, _format);
#ifndef OS_MAC_OLD
	reader.setAutoTransform(true);
#endif // OS_MAC_OLD
	_data = QPixmap::fromImageReader(&reader, Qt::ColorOnly);

	if (!_data.isNull()) {
		GlobalAcquiredSize += int64(_data.width()) * _data.height() * 4;
	}
	_forgot = false;
}

std::optional<Storage::Cache::Key> Image::cacheKey() const {
	return std::nullopt;
}

void Image::invalidateSizeCache() const {
	for (auto &pix : _sizesCache) {
		if (!pix.isNull()) {
			GlobalAcquiredSize -= int64(pix.width()) * pix.height() * 4;
		}
	}
	_sizesCache.clear();
}

Image::~Image() {
	invalidateSizeCache();
	if (!_data.isNull()) {
		GlobalAcquiredSize -= int64(_data.width()) * _data.height() * 4;
	}
}

void RemoteImage::doCheckload() const {
	if (!amLoading() || !_loader->finished()) return;

	QPixmap data = _loader->imagePixmap(shrinkBox());
	if (data.isNull()) {
		destroyLoaderDelayed(CancelledFileLoader);
		return;
	}

	if (!_data.isNull()) {
		GlobalAcquiredSize -= int64(_data.width()) * _data.height() * 4;
	}

	_format = _loader->imageFormat(shrinkBox());
	_data = data;
	_saved = _loader->bytes();
	const_cast<RemoteImage*>(this)->setInformation(_saved.size(), _data.width(), _data.height());
	GlobalAcquiredSize += int64(_data.width()) * _data.height() * 4;

	invalidateSizeCache();

	destroyLoaderDelayed();

	_forgot = false;
}

void RemoteImage::destroyLoaderDelayed(FileLoader *newValue) const {
	_loader->stop();
	auto loader = std::unique_ptr<FileLoader>(std::exchange(_loader, newValue));
	Auth().downloader().delayedDestroyLoader(std::move(loader));
}

void RemoteImage::loadLocal() {
	if (loaded() || amLoading()) return;

	_loader = createLoader(std::nullopt, LoadFromLocalOnly, true);
	if (_loader) _loader->start();
}

void RemoteImage::setImageBytes(
		const QByteArray &bytes,
		const QByteArray &bytesFormat) {
	if (!_data.isNull()) {
		GlobalAcquiredSize -= int64(_data.width()) * _data.height() * 4;
	}
	QByteArray fmt(bytesFormat);
	_data = App::pixmapFromImageInPlace(App::readImage(bytes, &fmt, false));
	if (!_data.isNull()) {
		GlobalAcquiredSize += int64(_data.width()) * _data.height() * 4;
		setInformation(bytes.size(), _data.width(), _data.height());
	}

	invalidateSizeCache();
	if (amLoading()) {
		destroyLoaderDelayed();
	}
	_saved = bytes;
	_format = fmt;
	_forgot = false;

	const auto location = this->location();
	if (!location.isNull()
		&& !bytes.isEmpty()
		&& bytes.size() <= Storage::kMaxFileInMemory) {
		Auth().data().cache().putIfEmpty(
			Data::StorageCacheKey(location),
			Storage::Cache::Database::TaggedValue(
				base::duplicate(bytes),
				Data::kImageCacheTag));
	}
}

bool RemoteImage::amLoading() const {
	return _loader && _loader != CancelledFileLoader;
}

void RemoteImage::automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	if (loaded()) return;

	if (_loader != CancelledFileLoader && item) {
		bool loadFromCloud = false;
		if (item->history()->peer->isUser()) {
			loadFromCloud = !(cAutoDownloadPhoto() & dbiadNoPrivate);
		} else {
			loadFromCloud = !(cAutoDownloadPhoto() & dbiadNoGroups);
		}

		if (_loader) {
			if (loadFromCloud) _loader->permitLoadFromCloud();
		} else {
			_loader = createLoader(
				origin,
				loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly,
				true);
			if (_loader) _loader->start();
		}
	}
}

void RemoteImage::automaticLoadSettingsChanged() {
	if (loaded() || _loader != CancelledFileLoader) return;
	_loader = 0;
}

void RemoteImage::load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	if (loaded()) return;

	if (!_loader) {
		_loader = createLoader(origin, LoadFromCloudOrLocal, false);
	}
	if (amLoading()) {
		_loader->start(loadFirst, prior);
	}
}

void RemoteImage::loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	if (_loader == CancelledFileLoader) {
		_loader = nullptr;
	}
	return load(origin, loadFirst, prior);
}

RemoteImage::~RemoteImage() {
	if (!_data.isNull()) {
		GlobalAcquiredSize -= int64(_data.width()) * _data.height() * 4;
	}
	if (amLoading()) {
		destroyLoaderDelayed();
	}
}

bool RemoteImage::loaded() const {
	doCheckload();
	return (!_data.isNull() || !_saved.isNull());
}

bool RemoteImage::displayLoading() const {
	return amLoading() && (!_loader->loadingLocal() || !_loader->autoLoading());
}

void RemoteImage::cancel() {
	if (!amLoading()) return;

	auto loader = std::exchange(_loader, CancelledFileLoader);
	loader->cancel();
	loader->stop();
	Auth().downloader().delayedDestroyLoader(std::unique_ptr<FileLoader>(loader));
}

float64 RemoteImage::progress() const {
	return amLoading() ? _loader->currentProgress() : (loaded() ? 1 : 0);
}

int32 RemoteImage::loadOffset() const {
	return amLoading() ? _loader->currentOffset() : 0;
}

StorageImage::StorageImage(const StorageImageLocation &location, int32 size)
: _location(location)
, _size(size) {
}

StorageImage::StorageImage(
	const StorageImageLocation &location,
	const QByteArray &bytes)
: _location(location)
, _size(bytes.size()) {
	setImageBytes(bytes);
}

std::optional<Storage::Cache::Key> StorageImage::cacheKey() const {
	return _location.isNull()
		? std::nullopt
		: base::make_optional(Data::StorageCacheKey(_location));
}

int32 StorageImage::countWidth() const {
	return _location.width();
}

int32 StorageImage::countHeight() const {
	return _location.height();
}

void StorageImage::setInformation(int32 size, int32 width, int32 height) {
	_size = size;
	_location.setSize(width, height);
}

FileLoader *StorageImage::createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	if (_location.isNull()) {
		return nullptr;
	}
	return new mtpFileLoader(
		&_location,
		origin,
		_size,
		fromCloud,
		autoLoading,
		Data::kImageCacheTag);
}

WebFileImage::WebFileImage(
	const WebFileLocation &location,
	QSize box,
	int size)
: _location(location)
, _box(box)
, _width(0)
, _height(0)
, _size(size) {
}

WebFileImage::WebFileImage(
	const WebFileLocation &location,
	int width,
	int height,
	int size)
: _location(location)
, _width(width)
, _height(height)
, _size(size) {
}

std::optional<Storage::Cache::Key> WebFileImage::cacheKey() const {
	return _location.isNull()
		? std::nullopt
		: base::make_optional(Data::WebDocumentCacheKey(_location));
}

int WebFileImage::countWidth() const {
	return _width;
}

int WebFileImage::countHeight() const {
	return _height;
}

void WebFileImage::setInformation(int size, int width, int height) {
	_size = size;
	_width = width;
	_height = height;
}

FileLoader *WebFileImage::createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	return _location.isNull()
		? nullptr
		: new mtpFileLoader(
			&_location,
			_size,
			fromCloud,
			autoLoading,
			Data::kImageCacheTag);
}

GeoPointImage::GeoPointImage(const GeoPointLocation &location)
: _location(location) {
}

std::optional<Storage::Cache::Key> GeoPointImage::cacheKey() const {
	return Data::GeoPointCacheKey(_location);
}

int GeoPointImage::countWidth() const {
	return _location.width * _location.scale;
}

int GeoPointImage::countHeight() const {
	return _location.height * _location.scale;
}

void GeoPointImage::setInformation(int size, int width, int height) {
	_size = size;
	_location.width = width;
	_location.height = height;
}

FileLoader *GeoPointImage::createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	return new mtpFileLoader(
			&_location,
			_size,
			fromCloud,
			autoLoading,
			Data::kImageCacheTag);
}

DelayedStorageImage::DelayedStorageImage()
: StorageImage(StorageImageLocation())
, _loadRequested(false)
, _loadCancelled(false)
, _loadFromCloud(false) {
}

DelayedStorageImage::DelayedStorageImage(int32 w, int32 h)
: StorageImage(StorageImageLocation(w, h, 0, 0, 0, 0, {}))
, _loadRequested(false)
, _loadCancelled(false)
, _loadFromCloud(false) {
}
//
//DelayedStorageImage::DelayedStorageImage(QByteArray &bytes)
//: StorageImage(StorageImageLocation(), bytes)
//, _loadRequested(false)
//, _loadCancelled(false)
//, _loadFromCloud(false) {
//}

void DelayedStorageImage::setDelayedStorageLocation(
		Data::FileOrigin origin,
		const StorageImageLocation location) {
	_location = location;
	if (_loadRequested) {
		if (!_loadCancelled) {
			if (_loadFromCloud) {
				load(origin);
			} else {
				loadLocal();
			}
		}
		_loadRequested = false;
	}
}

void DelayedStorageImage::automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	if (_location.isNull()) {
		if (!_loadCancelled && item) {
			bool loadFromCloud = false;
			if (item->history()->peer->isUser()) {
				loadFromCloud = !(cAutoDownloadPhoto() & dbiadNoPrivate);
			} else {
				loadFromCloud = !(cAutoDownloadPhoto() & dbiadNoGroups);
			}

			if (_loadRequested) {
				if (loadFromCloud) _loadFromCloud = loadFromCloud;
			} else {
				_loadFromCloud = loadFromCloud;
				_loadRequested = true;
			}
		}
	} else {
		StorageImage::automaticLoad(origin, item);
	}
}

void DelayedStorageImage::automaticLoadSettingsChanged() {
	if (_loadCancelled) _loadCancelled = false;
	StorageImage::automaticLoadSettingsChanged();
}

void DelayedStorageImage::load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	if (_location.isNull()) {
		_loadRequested = _loadFromCloud = true;
	} else {
		StorageImage::load(origin, loadFirst, prior);
	}
}

void DelayedStorageImage::loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	_loadCancelled = false;
	StorageImage::loadEvenCancelled(origin, loadFirst, prior);
}

bool DelayedStorageImage::displayLoading() const {
	return _location.isNull() ? true : StorageImage::displayLoading();
}

void DelayedStorageImage::cancel() {
	if (_loadRequested) {
		_loadRequested = false;
	}
	StorageImage::cancel();
}

WebImage::WebImage(const QString &url, QSize box)
: _url(url)
, _box(box)
, _size(0)
, _width(0)
, _height(0) {
}

WebImage::WebImage(const QString &url, int width, int height)
: _url(url)
, _size(0)
, _width(width)
, _height(height) {
}

std::optional<Storage::Cache::Key> WebImage::cacheKey() const {
	return Data::UrlCacheKey(_url);
}

void WebImage::setSize(int width, int height) {
	_width = width;
	_height = height;
}

int32 WebImage::countWidth() const {
	return _width;
}

int32 WebImage::countHeight() const {
	return _height;
}

void WebImage::setInformation(int32 size, int32 width, int32 height) {
	_size = size;
	setSize(width, height);
}

FileLoader *WebImage::createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	return new webFileLoader(
		_url,
		QString(),
		fromCloud,
		autoLoading,
		Data::kImageCacheTag);
}

namespace Images {
namespace details {

Image *Create(const QString &file, QByteArray format) {
	if (file.startsWith(qstr("http://"), Qt::CaseInsensitive)
		|| file.startsWith(qstr("https://"), Qt::CaseInsensitive)) {
		QString key = file;
		WebImages::const_iterator i = webImages.constFind(key);
		if (i == webImages.cend()) {
			i = webImages.insert(key, new WebImage(file));
		}
		return i.value();
	} else {
		QFileInfo f(file);
		QString key = qsl("//:%1//:%2//:").arg(f.size()).arg(f.lastModified().toTime_t()) + file;
		LocalImages::const_iterator i = localImages.constFind(key);
		if (i == localImages.cend()) {
			i = localImages.insert(key, new Image(file, format));
		}
		return i.value();
	}
}

Image *Create(const QString &url, QSize box) {
	QString key = qsl("//:%1//:%2//:").arg(box.width()).arg(box.height()) + url;
	auto i = webImages.constFind(key);
	if (i == webImages.cend()) {
		i = webImages.insert(key, new WebImage(url, box));
	}
	return i.value();
}

Image *Create(const QString &url, int width, int height) {
	QString key = url;
	auto i = webImages.constFind(key);
	if (i == webImages.cend()) {
		i = webImages.insert(key, new WebImage(url, width, height));
	} else {
		i.value()->setSize(width, height);
	}
	return i.value();
}

Image *Create(const QByteArray &filecontent, QByteArray format) {
	return new Image(filecontent, format);
}

Image *Create(const QPixmap &pixmap, QByteArray format) {
	return new Image(pixmap, format);
}

Image *Create(const QByteArray &filecontent, QByteArray format, const QPixmap &pixmap) {
	return new Image(filecontent, format, pixmap);
}

Image *Create(int32 width, int32 height) {
	return new DelayedStorageImage(width, height);
}

StorageImage *Create(const StorageImageLocation &location, int32 size) {
	const auto key = storageKey(location);
	auto i = storageImages.constFind(key);
	if (i == storageImages.cend()) {
		i = storageImages.insert(key, new StorageImage(location, size));
	} else {
		i.value()->refreshFileReference(location.fileReference());
	}
	return i.value();
}

StorageImage *Create(
		const StorageImageLocation &location,
		const QByteArray &bytes) {
	const auto key = storageKey(location);
	auto i = storageImages.constFind(key);
	if (i == storageImages.cend()) {
		i = storageImages.insert(key, new StorageImage(location, bytes));
	} else {
		i.value()->refreshFileReference(location.fileReference());
		if (!i.value()->loaded()) {
			i.value()->setImageBytes(bytes);
		}
	}
	return i.value();
}

QSize getImageSize(const QVector<MTPDocumentAttribute> &attributes) {
	for (const auto &attribute : attributes) {
		if (attribute.type() == mtpc_documentAttributeImageSize) {
			auto &size = attribute.c_documentAttributeImageSize();
			return QSize(size.vw.v, size.vh.v);
		}
	}
	return QSize();
}

Image *Create(const MTPDwebDocument &document) {
	const auto size = getImageSize(document.vattributes.v);
	if (size.isEmpty()) {
		return Image::Blank();
	}

	// We don't use size from WebDocument, because it is not reliable.
	// It can be > 0 and different from the real size that we get in upload.WebFile result.
	auto filesize = 0; // document.vsize.v;
	return Create(
		WebFileLocation(
			Global::WebFileDcId(),
			document.vurl.v,
			document.vaccess_hash.v),
		size.width(),
		size.height(),
		filesize);
}

Image *Create(const MTPDwebDocumentNoProxy &document) {
	const auto size = getImageSize(document.vattributes.v);
	if (size.isEmpty()) {
		return Image::Blank();
	}

	return Create(qs(document.vurl), size.width(), size.height());
}

Image *Create(const MTPDwebDocument &document, QSize box) {
	//const auto size = getImageSize(document.vattributes.v);
	//if (size.isEmpty()) {
	//	return Image::Blank();
	//}

	// We don't use size from WebDocument, because it is not reliable.
	// It can be > 0 and different from the real size that we get in upload.WebFile result.
	auto filesize = 0; // document.vsize.v;
	return Create(
		WebFileLocation(
			Global::WebFileDcId(),
			document.vurl.v,
			document.vaccess_hash.v),
		box,
		filesize);
}

Image *Create(const MTPDwebDocumentNoProxy &document, QSize box) {
	//const auto size = getImageSize(document.vattributes.v);
	//if (size.isEmpty()) {
	//	return Image::Blank();
	//}

	return Create(qs(document.vurl), box);
}

Image *Create(const MTPWebDocument &document) {
	switch (document.type()) {
	case mtpc_webDocument:
		return Create(document.c_webDocument());
	case mtpc_webDocumentNoProxy:
		return Create(document.c_webDocumentNoProxy());
	}
	Unexpected("Type in getImage(MTPWebDocument).");
}

Image *Create(const MTPWebDocument &document, QSize box) {
	switch (document.type()) {
	case mtpc_webDocument:
		return Create(document.c_webDocument(), box);
	case mtpc_webDocumentNoProxy:
		return Create(document.c_webDocumentNoProxy(), box);
	}
	Unexpected("Type in getImage(MTPWebDocument).");
}

WebFileImage *Create(
		const WebFileLocation &location,
		QSize box,
		int size) {
	auto key = storageKey(location);
	auto i = webFileImages.constFind(key);
	if (i == webFileImages.cend()) {
		i = webFileImages.insert(
			key,
			new WebFileImage(location, box, size));
	}
	return i.value();
}

WebFileImage *Create(
		const WebFileLocation &location,
		int width,
		int height,
		int size) {
	auto key = storageKey(location);
	auto i = webFileImages.constFind(key);
	if (i == webFileImages.cend()) {
		i = webFileImages.insert(
			key,
			new WebFileImage(location, width, height, size));
	}
	return i.value();
}

GeoPointImage *Create(const GeoPointLocation &location) {
	auto key = storageKey(location);
	auto i = geoPointImages.constFind(key);
	if (i == geoPointImages.cend()) {
		i = geoPointImages.insert(
			key,
			new GeoPointImage(location));
	}
	return i.value();
}

} // namespace detals
} // namespace Images
