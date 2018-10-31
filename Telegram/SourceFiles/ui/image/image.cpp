/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/image.h"

#include "ui/image/image_source.h"
#include "core/media_active_cache.h"
#include "storage/cache/storage_cache_database.h"
#include "data/data_session.h"
#include "auth_session.h"

using namespace Images;

namespace Images {
namespace {

// After 128 MB of unpacked images we try to clear some memory.
constexpr auto kMemoryForCache = 128 * 1024 * 1024;

QMap<QString, Image*> LocalFileImages;
QMap<QString, Image*> WebUrlImages;
QMap<StorageKey, Image*> StorageImages;
QMap<StorageKey, Image*> WebCachedImages;
QMap<StorageKey, Image*> GeoPointImages;

int64 ComputeUsage(QSize size) {
	return int64(size.width()) * size.height() * 4;
}

int64 ComputeUsage(const QPixmap &image) {
	return ComputeUsage(image.size());
}

int64 ComputeUsage(const QImage &image) {
	return ComputeUsage(image.size());
}

Core::MediaActiveCache<const Image> &ActiveCache() {
	static auto Instance = Core::MediaActiveCache<const Image>(
		kMemoryForCache,
		[](const Image *image) { image->unload(); });
	return Instance;
}

uint64 PixKey(int width, int height, Options options) {
	return static_cast<uint64>(width)
		| (static_cast<uint64>(height) << 24)
		| (static_cast<uint64>(options) << 48);
}

uint64 SinglePixKey(Options options) {
	return PixKey(0, 0, options);
}

} // namespace

void ClearRemote() {
	for (auto image : base::take(StorageImages)) {
		delete image;
	}
	for (auto image : base::take(WebUrlImages)) {
		delete image;
	}
	for (auto image : base::take(WebCachedImages)) {
		delete image;
	}
	for (auto image : base::take(GeoPointImages)) {
		delete image;
	}
}

void ClearAll() {
	ActiveCache().clear();
	for (auto image : base::take(LocalFileImages)) {
		delete image;
	}
	ClearRemote();
}

ImagePtr Create(const QString &file, QByteArray format) {
	if (file.startsWith(qstr("http://"), Qt::CaseInsensitive)
		|| file.startsWith(qstr("https://"), Qt::CaseInsensitive)) {
		const auto key = file;
		auto i = WebUrlImages.constFind(key);
		if (i == WebUrlImages.cend()) {
			i = WebUrlImages.insert(
				key,
				new Image(std::make_unique<WebUrlSource>(file)));
		}
		return ImagePtr(i.value());
	} else {
		QFileInfo f(file);
		const auto key = qsl("//:%1//:%2//:"
		).arg(f.size()
		).arg(f.lastModified().toTime_t()
		) + file;
		auto i = LocalFileImages.constFind(key);
		if (i == LocalFileImages.cend()) {
			i = LocalFileImages.insert(
				key,
				new Image(std::make_unique<LocalFileSource>(
					file,
					QByteArray(),
					format)));
		}
		return ImagePtr(i.value());
	}
}

ImagePtr Create(const QString &url, QSize box) {
	const auto key = qsl("//:%1//:%2//:").arg(box.width()).arg(box.height()) + url;
	auto i = WebUrlImages.constFind(key);
	if (i == WebUrlImages.cend()) {
		i = WebUrlImages.insert(
			key,
			new Image(std::make_unique<WebUrlSource>(url, box)));
	}
	return ImagePtr(i.value());
}

ImagePtr Create(const QString &url, int width, int height) {
	const auto key = url;
	auto i = WebUrlImages.constFind(key);
	if (i == WebUrlImages.cend()) {
		i = WebUrlImages.insert(
			key,
			new Image(std::make_unique<WebUrlSource>(url, width, height)));
	} else {
		i.value()->setInformation(0, width, height);
	}
	return ImagePtr(i.value());
}

ImagePtr Create(const QByteArray &filecontent, QByteArray format) {
	auto image = App::readImage(filecontent, &format, false);
	return Create(filecontent, format, std::move(image));
}

ImagePtr Create(QImage &&image, QByteArray format) {
	return ImagePtr(new Image(std::make_unique<ImageSource>(
		std::move(image),
		format)));
}

ImagePtr Create(
		const QByteArray &filecontent,
		QByteArray format,
		QImage &&image) {
	return ImagePtr(new Image(std::make_unique<LocalFileSource>(
		QString(),
		filecontent,
		format,
		std::move(image))));
}

ImagePtr Create(int width, int height) {
	return ImagePtr(new Image(std::make_unique<DelayedStorageSource>(
		width,
		height)));
}

ImagePtr Create(const StorageImageLocation &location, int size) {
	const auto key = storageKey(location);
	auto i = StorageImages.constFind(key);
	if (i == StorageImages.cend()) {
		i = StorageImages.insert(
			key,
			new Image(std::make_unique<StorageSource>(location, size)));
	} else {
		i.value()->refreshFileReference(location.fileReference());
	}
	return ImagePtr(i.value());
}

ImagePtr Create(
		const StorageImageLocation &location,
		const QByteArray &bytes) {
	const auto key = storageKey(location);
	auto i = StorageImages.constFind(key);
	if (i == StorageImages.cend()) {
		i = StorageImages.insert(
			key,
			new Image(std::make_unique<StorageSource>(
				location,
				bytes.size())));
	} else {
		i.value()->refreshFileReference(location.fileReference());
	}
	i.value()->setImageBytes(bytes);
	return ImagePtr(i.value());
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

ImagePtr Create(const MTPDwebDocument &document) {
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

ImagePtr Create(const MTPDwebDocumentNoProxy &document) {
	const auto size = getImageSize(document.vattributes.v);
	if (size.isEmpty()) {
		return Image::Blank();
	}

	return Create(qs(document.vurl), size.width(), size.height());
}

ImagePtr Create(const MTPDwebDocument &document, QSize box) {
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

ImagePtr Create(const MTPDwebDocumentNoProxy &document, QSize box) {
	//const auto size = getImageSize(document.vattributes.v);
	//if (size.isEmpty()) {
	//	return Image::Blank();
	//}

	return Create(qs(document.vurl), box);
}

ImagePtr Create(const MTPWebDocument &document) {
	switch (document.type()) {
	case mtpc_webDocument:
		return Create(document.c_webDocument());
	case mtpc_webDocumentNoProxy:
		return Create(document.c_webDocumentNoProxy());
	}
	Unexpected("Type in getImage(MTPWebDocument).");
}

ImagePtr Create(const MTPWebDocument &document, QSize box) {
	switch (document.type()) {
	case mtpc_webDocument:
		return Create(document.c_webDocument(), box);
	case mtpc_webDocumentNoProxy:
		return Create(document.c_webDocumentNoProxy(), box);
	}
	Unexpected("Type in getImage(MTPWebDocument).");
}

ImagePtr Create(
		const WebFileLocation &location,
		QSize box,
		int size) {
	const auto key = storageKey(location);
	auto i = WebCachedImages.constFind(key);
	if (i == WebCachedImages.cend()) {
		i = WebCachedImages.insert(
			key,
			new Image(std::make_unique<WebCachedSource>(
				location,
				box,
				size)));
	}
	return ImagePtr(i.value());
}

ImagePtr Create(
		const WebFileLocation &location,
		int width,
		int height,
		int size) {
	const auto key = storageKey(location);
	auto i = WebCachedImages.constFind(key);
	if (i == WebCachedImages.cend()) {
		i = WebCachedImages.insert(
			key,
			new Image(std::make_unique<WebCachedSource>(
				location,
				width,
				height,
				size)));
	}
	return ImagePtr(i.value());
}

ImagePtr Create(const GeoPointLocation &location) {
	const auto key = storageKey(location);
	auto i = GeoPointImages.constFind(key);
	if (i == GeoPointImages.cend()) {
		i = GeoPointImages.insert(
			key,
			new Image(std::make_unique<GeoPointSource>(location)));
	}
	return ImagePtr(i.value());
}

} // namespace Images

Image::Image(std::unique_ptr<Source> &&source)
: _source(std::move(source)) {
}

void Image::replaceSource(std::unique_ptr<Source> &&source) {
	_source = std::move(source);
}

ImagePtr Image::Blank() {
	static const auto blankImage = [] {
		const auto factor = cIntRetinaFactor();
		auto data = QImage(
			factor,
			factor,
			QImage::Format_ARGB32_Premultiplied);
		data.fill(Qt::transparent);
		data.setDevicePixelRatio(cRetinaFactor());
		return Create(
			std::move(data),
			"GIF");
	}();
	return blankImage;
}

bool Image::isNull() const {
	return (this == Blank().get());
}

const QPixmap &Image::pix(
		Data::FileOrigin origin,
		int32 w,
		int32 h) const {
	checkSource();

	if (w <= 0 || !width() || !height()) {
        w = width();
    } else {
        w *= cIntRetinaFactor();
        h *= cIntRetinaFactor();
    }
	auto options = Option::Smooth | Option::None;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
        p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		ActiveCache().increment(ComputeUsage(*i));
	}
	return i.value();
}

const QPixmap &Image::pixRounded(
		Data::FileOrigin origin,
		int32 w,
		int32 h,
		ImageRoundRadius radius,
		RectParts corners) const {
	checkSource();

	if (w <= 0 || !width() || !height()) {
		w = width();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Option::Smooth | Option::None;
	auto cornerOptions = [](RectParts corners) {
		return (corners & RectPart::TopLeft ? Option::RoundedTopLeft : Option::None)
			| (corners & RectPart::TopRight ? Option::RoundedTopRight : Option::None)
			| (corners & RectPart::BottomLeft ? Option::RoundedBottomLeft : Option::None)
			| (corners & RectPart::BottomRight ? Option::RoundedBottomRight : Option::None);
	};
	if (radius == ImageRoundRadius::Large) {
		options |= Option::RoundedLarge | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Small) {
		options |= Option::RoundedSmall | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Ellipse) {
		options |= Option::Circled | cornerOptions(corners);
	}
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		ActiveCache().increment(ComputeUsage(*i));
	}
	return i.value();
}

const QPixmap &Image::pixCircled(
		Data::FileOrigin origin,
		int32 w,
		int32 h) const {
	checkSource();

	if (w <= 0 || !width() || !height()) {
		w = width();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Option::Smooth | Option::Circled;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		ActiveCache().increment(ComputeUsage(*i));
	}
	return i.value();
}

const QPixmap &Image::pixBlurredCircled(
		Data::FileOrigin origin,
		int32 w,
		int32 h) const {
	checkSource();

	if (w <= 0 || !width() || !height()) {
		w = width();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Option::Smooth | Option::Circled | Option::Blurred;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		ActiveCache().increment(ComputeUsage(*i));
	}
	return i.value();
}

const QPixmap &Image::pixBlurred(
		Data::FileOrigin origin,
		int32 w,
		int32 h) const {
	checkSource();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Option::Smooth | Option::Blurred;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		ActiveCache().increment(ComputeUsage(*i));
	}
	return i.value();
}

const QPixmap &Image::pixColored(
		Data::FileOrigin origin,
		style::color add,
		int32 w,
		int32 h) const {
	checkSource();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Option::Smooth | Option::Colored;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixColoredNoCache(origin, add, w, h, true);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		ActiveCache().increment(ComputeUsage(*i));
	}
	return i.value();
}

const QPixmap &Image::pixBlurredColored(
		Data::FileOrigin origin,
		style::color add,
		int32 w,
		int32 h) const {
	checkSource();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}
	auto options = Option::Blurred | Option::Smooth | Option::Colored;
	auto k = PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixBlurredColoredNoCache(origin, add, w, h);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		ActiveCache().increment(ComputeUsage(*i));
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
	checkSource();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}

	auto options = Option::Smooth | Option::None;
	auto cornerOptions = [](RectParts corners) {
		return (corners & RectPart::TopLeft ? Option::RoundedTopLeft : Option::None)
			| (corners & RectPart::TopRight ? Option::RoundedTopRight : Option::None)
			| (corners & RectPart::BottomLeft ? Option::RoundedBottomLeft : Option::None)
			| (corners & RectPart::BottomRight ? Option::RoundedBottomRight : Option::None);
	};
	if (radius == ImageRoundRadius::Large) {
		options |= Option::RoundedLarge | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Small) {
		options |= Option::RoundedSmall | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Ellipse) {
		options |= Option::Circled | cornerOptions(corners);
	}
	if (colored) {
		options |= Option::Colored;
	}

	auto k = SinglePixKey(options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend() || i->width() != (outerw * cIntRetinaFactor()) || i->height() != (outerh * cIntRetinaFactor())) {
		if (i != _sizesCache.cend()) {
			ActiveCache().decrement(ComputeUsage(*i));
		}
		auto p = pixNoCache(origin, w, h, options, outerw, outerh, colored);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		ActiveCache().increment(ComputeUsage(*i));
	}
	return i.value();
}

const QPixmap &Image::pixBlurredSingle(
		Data::FileOrigin origin,
		int32 w,
		int32 h,
		int32 outerw,
		int32 outerh,
		ImageRoundRadius radius,
		RectParts corners) const {
	checkSource();

	if (w <= 0 || !width() || !height()) {
		w = width() * cIntRetinaFactor();
	} else {
		w *= cIntRetinaFactor();
		h *= cIntRetinaFactor();
	}

	auto options = Option::Smooth | Option::Blurred;
	auto cornerOptions = [](RectParts corners) {
		return (corners & RectPart::TopLeft ? Option::RoundedTopLeft : Option::None)
			| (corners & RectPart::TopRight ? Option::RoundedTopRight : Option::None)
			| (corners & RectPart::BottomLeft ? Option::RoundedBottomLeft : Option::None)
			| (corners & RectPart::BottomRight ? Option::RoundedBottomRight : Option::None);
	};
	if (radius == ImageRoundRadius::Large) {
		options |= Option::RoundedLarge | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Small) {
		options |= Option::RoundedSmall | cornerOptions(corners);
	} else if (radius == ImageRoundRadius::Ellipse) {
		options |= Option::Circled | cornerOptions(corners);
	}

	auto k = SinglePixKey(options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend() || i->width() != (outerw * cIntRetinaFactor()) || i->height() != (outerh * cIntRetinaFactor())) {
		if (i != _sizesCache.cend()) {
			ActiveCache().decrement(ComputeUsage(*i));
		}
		auto p = pixNoCache(origin, w, h, options, outerw, outerh);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		ActiveCache().increment(ComputeUsage(*i));
	}
	return i.value();
}

QPixmap Image::pixNoCache(
		Data::FileOrigin origin,
		int w,
		int h,
		Options options,
		int outerw,
		int outerh,
		const style::color *colored) const {
	if (!loading()) {
		const_cast<Image*>(this)->load(origin);
	}
	checkSource();

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

		auto corners = [](Options options) {
			return ((options & Option::RoundedTopLeft) ? RectPart::TopLeft : RectPart::None)
				| ((options & Option::RoundedTopRight) ? RectPart::TopRight : RectPart::None)
				| ((options & Option::RoundedBottomLeft) ? RectPart::BottomLeft : RectPart::None)
				| ((options & Option::RoundedBottomRight) ? RectPart::BottomRight : RectPart::None);
		};
		if (options & Option::Circled) {
			prepareCircle(result);
		} else if (options & Option::RoundedLarge) {
			prepareRound(result, ImageRoundRadius::Large, corners(options));
		} else if (options & Option::RoundedSmall) {
			prepareRound(result, ImageRoundRadius::Small, corners(options));
		}
		if (options & Option::Colored) {
			Assert(colored != nullptr);
			result = prepareColored(*colored, std::move(result));
		}
		return App::pixmapFromImageInPlace(std::move(result));
	}

	return App::pixmapFromImageInPlace(prepare(_data, w, h, options, outerw, outerh, colored));
}

QPixmap Image::pixColoredNoCache(
		Data::FileOrigin origin,
		style::color add,
		int32 w,
		int32 h,
		bool smooth) const {
	if (!loading()) {
		const_cast<Image*>(this)->load(origin);
	}
	checkSource();

	if (_data.isNull()) {
		return Blank()->pix(origin);
	}

	auto img = _data;
	if (w <= 0 || !width() || !height() || (w == width() && (h <= 0 || h == height()))) {
		return App::pixmapFromImageInPlace(prepareColored(add, std::move(img)));
	}
	if (h <= 0) {
		return App::pixmapFromImageInPlace(prepareColored(add, img.scaledToWidth(w, smooth ? Qt::SmoothTransformation : Qt::FastTransformation)));
	}
	return App::pixmapFromImageInPlace(prepareColored(add, img.scaled(w, h, Qt::IgnoreAspectRatio, smooth ? Qt::SmoothTransformation : Qt::FastTransformation)));
}

QPixmap Image::pixBlurredColoredNoCache(
		Data::FileOrigin origin,
		style::color add,
		int32 w,
		int32 h) const {
	if (!loading()) {
		const_cast<Image*>(this)->load(origin);
	}
	checkSource();

	if (_data.isNull()) {
		return Blank()->pix(origin);
	}

	auto img = prepareBlur(_data);
	if (h <= 0) {
		img = img.scaledToWidth(w, Qt::SmoothTransformation);
	} else {
		img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	}

	return App::pixmapFromImageInPlace(prepareColored(add, img));
}

std::optional<Storage::Cache::Key> Image::cacheKey() const {
	return _source->cacheKey();
}

bool Image::loaded() const {
	checkSource();
	return !_data.isNull();
}

void Image::checkSource() const {
	auto data = _source->takeLoaded();
	if (_data.isNull() && !data.isNull()) {
		invalidateSizeCache();
		_data = std::move(data);
		ActiveCache().increment(ComputeUsage(_data));
	}

	ActiveCache().up(this);
}

void Image::unload() const {
	_source->unload();
	invalidateSizeCache();
	ActiveCache().decrement(ComputeUsage(_data));
	_data = QImage();
}

void Image::setDelayedStorageLocation(
		Data::FileOrigin origin,
		const StorageImageLocation &location) {
	_source->setDelayedStorageLocation(location);
	if (!loaded()) {
		_source->performDelayedLoad(origin);
	}
}

void Image::setImageBytes(const QByteArray &bytes) {
	_source->setImageBytes(bytes);
	checkSource();
}

void Image::invalidateSizeCache() const {
	auto &cache = ActiveCache();
	for (const auto &image : std::as_const(_sizesCache)) {
		cache.decrement(ComputeUsage(image));
	}
	_sizesCache.clear();
}

Image::~Image() {
	unload();
	ActiveCache().remove(this);
}
