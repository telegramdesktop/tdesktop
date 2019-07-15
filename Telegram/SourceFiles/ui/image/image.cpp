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
#include "data/data_file_origin.h"
#include "chat_helpers/stickers.h"
#include "auth_session.h"

using namespace Images;

namespace Images {
namespace {

// After 128 MB of unpacked images we try to clear some memory.
constexpr auto kMemoryForCache = 128 * 1024 * 1024;

std::map<QString, std::unique_ptr<Image>> LocalFileImages;
std::map<QString, std::unique_ptr<Image>> WebUrlImages;
std::unordered_map<InMemoryKey, std::unique_ptr<Image>> StorageImages;
std::unordered_map<InMemoryKey, std::unique_ptr<Image>> WebCachedImages;
std::unordered_map<InMemoryKey, std::unique_ptr<Image>> GeoPointImages;

int64 ComputeUsage(QSize size) {
	return int64(size.width()) * size.height() * 4;
}

int64 ComputeUsage(const QPixmap &image) {
	return ComputeUsage(image.size());
}

int64 ComputeUsage(const QImage &image) {
	return ComputeUsage(image.size());
}

[[nodiscard]] Core::MediaActiveCache<const Image> &ActiveCache() {
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
	base::take(StorageImages);
	base::take(WebUrlImages);
	base::take(WebCachedImages);
	base::take(GeoPointImages);
}

void ClearAll() {
	ActiveCache().clear();
	base::take(LocalFileImages);
	ClearRemote();
}

ImagePtr Create(const QString &file, QByteArray format) {
	if (file.startsWith(qstr("http://"), Qt::CaseInsensitive)
		|| file.startsWith(qstr("https://"), Qt::CaseInsensitive)) {
		const auto &key = file;
		const auto i = WebUrlImages.find(key);
		const auto image = (i != end(WebUrlImages))
			? i->second.get()
			: WebUrlImages.emplace(
				key,
				std::make_unique<Image>(std::make_unique<WebUrlSource>(file))
			).first->second.get();
		return ImagePtr(image);
	}
	QFileInfo f(file);
	const auto key = qsl("//:%1//:%2//:"
	).arg(f.size()
	).arg(f.lastModified().toTime_t()
	) + file;
	const auto i = LocalFileImages.find(key);
	const auto image = (i != end(LocalFileImages))
		? i->second.get()
		: LocalFileImages.emplace(
			key,
			std::make_unique<Image>(
				std::make_unique<LocalFileSource>(
					file,
					QByteArray(),
					format))
		).first->second.get();
	return ImagePtr(image);
}

ImagePtr Create(const QString &url, QSize box) {
	const auto key = qsl("//:%1//:%2//:").arg(box.width()).arg(box.height()) + url;
	const auto i = WebUrlImages.find(key);
	const auto image = (i != end(WebUrlImages))
		? i->second.get()
		: WebUrlImages.emplace(
			key,
			std::make_unique<Image>(std::make_unique<WebUrlSource>(url, box))
		).first->second.get();
	return ImagePtr(image);
}

ImagePtr Create(const QString &url, int width, int height) {
	const auto &key = url;
	const auto i = WebUrlImages.find(key);
	const auto found = (i != end(WebUrlImages));
	const auto image = found
		? i->second.get()
		: WebUrlImages.emplace(
			key,
			std::make_unique<Image>(
				std::make_unique<WebUrlSource>(url, width, height))
		).first->second.get();
	if (found) {
		image->setInformation(0, width, height);
	}
	return ImagePtr(image);
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

template <typename SourceType>
ImagePtr Create(
		const StorageImageLocation &location,
		int size,
		const QByteArray &bytes) {
	if (!location.valid()) {
		return ImagePtr();
	}
	const auto key = inMemoryKey(location);
	const auto i = StorageImages.find(key);
	const auto found = (i != end(StorageImages));
	const auto image = found
		? i->second.get()
		: StorageImages.emplace(
			key,
			std::make_unique<Image>(
				std::make_unique<SourceType>(location, size))
		).first->second.get();
	if (found) {
		image->refreshFileReference(location.fileReference());
	}
	if (!bytes.isEmpty()) {
		image->setImageBytes(bytes);
	}
	return ImagePtr(image);

}

ImagePtr Create(const StorageImageLocation &location, int size) {
	return Create<StorageSource>(location, size, QByteArray());
}

ImagePtr Create(
		const StorageImageLocation &location,
		const QByteArray &bytes) {
	return Create<StorageSource>(location, bytes.size(), bytes);
}

struct CreateStorageImage {
	ImagePtr operator()(
			const StorageImageLocation &location,
			int size) {
		return Create(location, size);
	}
	ImagePtr operator()(
			const StorageImageLocation &location,
			const QByteArray &bytes) {
		return Create(location, bytes);
	}
};

struct CreateSetThumbnail {
	using Source = Stickers::ThumbnailSource;
	ImagePtr operator()(
			const StorageImageLocation &location,
			int size) {
		return Create<Source>(location, size, QByteArray());
	}
	ImagePtr operator()(
			const StorageImageLocation &location,
			const QByteArray &bytes) {
		return Create<Source>(location, bytes.size(), bytes);
	}
};

template <typename CreateLocation, typename Method = CreateStorageImage>
ImagePtr CreateFromPhotoSize(
		CreateLocation &&createLocation,
		const MTPPhotoSize &size,
		Method method = Method()) {
	return size.match([&](const MTPDphotoSize &data) {
		const auto &location = data.vlocation().c_fileLocationToBeDeprecated();
		return method(
			StorageImageLocation(
				createLocation(data.vtype(), location),
				data.vw().v,
				data.vh().v),
			data.vsize().v);
	}, [&](const MTPDphotoCachedSize &data) {
		const auto bytes = qba(data.vbytes());
		const auto &location = data.vlocation().c_fileLocationToBeDeprecated();
		return method(
			StorageImageLocation(
				createLocation(data.vtype(), location),
				data.vw().v,
				data.vh().v),
			bytes);
	}, [&](const MTPDphotoStrippedSize &data) {
		const auto bytes = qba(data.vbytes());
		if (bytes.size() < 3 || bytes[0] != '\x01') {
			return ImagePtr();
		}
		const char header[] = "\xff\xd8\xff\xe0\x00\x10\x4a\x46\x49"
"\x46\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xdb\x00\x43\x00\x28\x1c"
"\x1e\x23\x1e\x19\x28\x23\x21\x23\x2d\x2b\x28\x30\x3c\x64\x41\x3c\x37\x37"
"\x3c\x7b\x58\x5d\x49\x64\x91\x80\x99\x96\x8f\x80\x8c\x8a\xa0\xb4\xe6\xc3"
"\xa0\xaa\xda\xad\x8a\x8c\xc8\xff\xcb\xda\xee\xf5\xff\xff\xff\x9b\xc1\xff"
"\xff\xff\xfa\xff\xe6\xfd\xff\xf8\xff\xdb\x00\x43\x01\x2b\x2d\x2d\x3c\x35"
"\x3c\x76\x41\x41\x76\xf8\xa5\x8c\xa5\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8"
"\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8"
"\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8\xf8"
"\xf8\xf8\xf8\xf8\xf8\xff\xc0\x00\x11\x08\x00\x00\x00\x00\x03\x01\x22\x00"
"\x02\x11\x01\x03\x11\x01\xff\xc4\x00\x1f\x00\x00\x01\x05\x01\x01\x01\x01"
"\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08"
"\x09\x0a\x0b\xff\xc4\x00\xb5\x10\x00\x02\x01\x03\x03\x02\x04\x03\x05\x05"
"\x04\x04\x00\x00\x01\x7d\x01\x02\x03\x00\x04\x11\x05\x12\x21\x31\x41\x06"
"\x13\x51\x61\x07\x22\x71\x14\x32\x81\x91\xa1\x08\x23\x42\xb1\xc1\x15\x52"
"\xd1\xf0\x24\x33\x62\x72\x82\x09\x0a\x16\x17\x18\x19\x1a\x25\x26\x27\x28"
"\x29\x2a\x34\x35\x36\x37\x38\x39\x3a\x43\x44\x45\x46\x47\x48\x49\x4a\x53"
"\x54\x55\x56\x57\x58\x59\x5a\x63\x64\x65\x66\x67\x68\x69\x6a\x73\x74\x75"
"\x76\x77\x78\x79\x7a\x83\x84\x85\x86\x87\x88\x89\x8a\x92\x93\x94\x95\x96"
"\x97\x98\x99\x9a\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xb2\xb3\xb4\xb5\xb6"
"\xb7\xb8\xb9\xba\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xd2\xd3\xd4\xd5\xd6"
"\xd7\xd8\xd9\xda\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xf1\xf2\xf3\xf4"
"\xf5\xf6\xf7\xf8\xf9\xfa\xff\xc4\x00\x1f\x01\x00\x03\x01\x01\x01\x01\x01"
"\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08"
"\x09\x0a\x0b\xff\xc4\x00\xb5\x11\x00\x02\x01\x02\x04\x04\x03\x04\x07\x05"
"\x04\x04\x00\x01\x02\x77\x00\x01\x02\x03\x11\x04\x05\x21\x31\x06\x12\x41"
"\x51\x07\x61\x71\x13\x22\x32\x81\x08\x14\x42\x91\xa1\xb1\xc1\x09\x23\x33"
"\x52\xf0\x15\x62\x72\xd1\x0a\x16\x24\x34\xe1\x25\xf1\x17\x18\x19\x1a\x26"
"\x27\x28\x29\x2a\x35\x36\x37\x38\x39\x3a\x43\x44\x45\x46\x47\x48\x49\x4a"
"\x53\x54\x55\x56\x57\x58\x59\x5a\x63\x64\x65\x66\x67\x68\x69\x6a\x73\x74"
"\x75\x76\x77\x78\x79\x7a\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x92\x93\x94"
"\x95\x96\x97\x98\x99\x9a\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xb2\xb3\xb4"
"\xb5\xb6\xb7\xb8\xb9\xba\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xd2\xd3\xd4"
"\xd5\xd6\xd7\xd8\xd9\xda\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xf2\xf3\xf4"
"\xf5\xf6\xf7\xf8\xf9\xfa\xff\xda\x00\x0c\x03\x01\x00\x02\x11\x03\x11\x00"
"\x3f\x00";
		const char footer[] = "\xff\xd9";
		auto real = QByteArray(header, sizeof(header) - 1);
		real[164] = bytes[1];
		real[166] = bytes[2];
		const auto ready = real
			+ bytes.mid(3)
			+ QByteArray::fromRawData(footer, sizeof(footer) - 1);
		auto image = App::readImage(ready);
		return !image.isNull()
			? Images::Create(std::move(image), "JPG")
			: ImagePtr();
	}, [&](const MTPDphotoSizeEmpty &) {
		return ImagePtr();
	});
}

ImagePtr Create(const MTPDstickerSet &set, const MTPPhotoSize &size) {
	const auto thumbDcId = set.vthumb_dc_id();
	const auto create = [&](
			const MTPstring &thumbSize,
			const MTPDfileLocationToBeDeprecated &location) {
		return StorageFileLocation(
			thumbDcId->v,
			Auth().userId(),
			MTP_inputStickerSetThumb(
				MTP_inputStickerSetID(set.vid(), set.vaccess_hash()),
				location.vvolume_id(),
				location.vlocal_id()));
	};
	return thumbDcId
		? CreateFromPhotoSize(create, size, CreateSetThumbnail())
		: ImagePtr();
}

ImagePtr CreateStickerSetThumbnail(const StorageImageLocation &location) {
	return CreateSetThumbnail()(location, 0);
}

ImagePtr Create(const MTPDphoto &photo, const MTPPhotoSize &size) {
	const auto create = [&](
			const MTPstring &thumbSize,
			const MTPDfileLocationToBeDeprecated &location) {
		return StorageFileLocation(
			photo.vdc_id().v,
			Auth().userId(),
			MTP_inputPhotoFileLocation(
				photo.vid(),
				photo.vaccess_hash(),
				photo.vfile_reference(),
				thumbSize));
	};
	return CreateFromPhotoSize(create, size);
}

ImagePtr Create(const MTPDdocument &document, const MTPPhotoSize &size) {
	const auto create = [&](
			const MTPstring &thumbSize,
			const MTPDfileLocationToBeDeprecated &location) {
		return StorageFileLocation(
			document.vdc_id().v,
			Auth().userId(),
			MTP_inputDocumentFileLocation(
				document.vid(),
				document.vaccess_hash(),
				document.vfile_reference(),
				thumbSize));
	};
	return CreateFromPhotoSize(create, size);
}

QSize getImageSize(const QVector<MTPDocumentAttribute> &attributes) {
	for (const auto &attribute : attributes) {
		if (attribute.type() == mtpc_documentAttributeImageSize) {
			auto &size = attribute.c_documentAttributeImageSize();
			return QSize(size.vw().v, size.vh().v);
		}
	}
	return QSize();
}

ImagePtr Create(const MTPDwebDocument &document) {
	const auto size = getImageSize(document.vattributes().v);
	if (size.isEmpty()) {
		return ImagePtr();
	}

	// We don't use size from WebDocument, because it is not reliable.
	// It can be > 0 and different from the real size that we get in upload.WebFile result.
	auto filesize = 0; // document.vsize().v;
	return Create(
		WebFileLocation(
			document.vurl().v,
			document.vaccess_hash().v),
		size.width(),
		size.height(),
		filesize);
}

ImagePtr Create(const MTPDwebDocumentNoProxy &document) {
	const auto size = getImageSize(document.vattributes().v);
	if (size.isEmpty()) {
		return ImagePtr();
	}

	return Create(qs(document.vurl()), size.width(), size.height());
}

ImagePtr Create(const MTPDwebDocument &document, QSize box) {
	//const auto size = getImageSize(document.vattributes().v);
	//if (size.isEmpty()) {
	//	return ImagePtr();
	//}

	// We don't use size from WebDocument, because it is not reliable.
	// It can be > 0 and different from the real size that we get in upload.WebFile result.
	auto filesize = 0; // document.vsize().v;
	return Create(
		WebFileLocation(
			document.vurl().v,
			document.vaccess_hash().v),
		box,
		filesize);
}

ImagePtr Create(const MTPDwebDocumentNoProxy &document, QSize box) {
	//const auto size = getImageSize(document.vattributes().v);
	//if (size.isEmpty()) {
	//	return ImagePtr();
	//}

	return Create(qs(document.vurl()), box);
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
	const auto key = inMemoryKey(location);
	const auto i = WebCachedImages.find(key);
	const auto image = (i != end(WebCachedImages))
		? i->second.get()
		: WebCachedImages.emplace(
			key,
			std::make_unique<Image>(std::make_unique<WebCachedSource>(
				location,
				box,
				size))
		).first->second.get();
	return ImagePtr(image);
}

ImagePtr Create(
		const WebFileLocation &location,
		int width,
		int height,
		int size) {
	const auto key = inMemoryKey(location);
	const auto i = WebCachedImages.find(key);
	const auto image = (i != end(WebCachedImages))
		? i->second.get()
		: WebCachedImages.emplace(
			key,
			std::make_unique<Image>(std::make_unique<WebCachedSource>(
				location,
				width,
				height,
				size))
		).first->second.get();
	return ImagePtr(image);
}

ImagePtr Create(const GeoPointLocation &location) {
	const auto key = inMemoryKey(location);
	const auto i = GeoPointImages.find(key);
	const auto image = (i != end(GeoPointImages))
		? i->second.get()
		: GeoPointImages.emplace(
			key,
			std::make_unique<Image>(
				std::make_unique<GeoPointSource>(location))
		).first->second.get();
	return ImagePtr(image);
}

} // namespace Images

Image::Image(std::unique_ptr<Source> &&source)
: _source(std::move(source)) {
}

void Image::replaceSource(std::unique_ptr<Source> &&source) {
	const auto width = _source->width();
	const auto height = _source->height();
	if (width > 0 && height > 0) {
		source->setInformation(_source->bytesSize(), width, height);
	}
	_source = std::move(source);
}

not_null<Image*> Image::Empty() {
	static auto result = [] {
		const auto factor = cIntRetinaFactor();
		auto data = QImage(
			factor,
			factor,
			QImage::Format_ARGB32_Premultiplied);
		data.fill(Qt::transparent);
		data.setDevicePixelRatio(cRetinaFactor());
		return Image(std::make_unique<ImageSource>(std::move(data), "GIF"));
	}();
	return &result;
}

not_null<Image*> Image::BlankMedia() {
	static auto result = [] {
		const auto factor = cIntRetinaFactor();
		auto data = QImage(
			factor,
			factor,
			QImage::Format_ARGB32_Premultiplied);
		data.fill(Qt::black);
		data.setDevicePixelRatio(cRetinaFactor());
		return Image(std::make_unique<ImageSource>(std::move(data), "GIF"));
	}();
	return &result;
}

bool Image::isNull() const {
	return (this == Empty());
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
		return Empty()->pixNoCache(origin, w, h, options, outerw, outerh);
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
		return Empty()->pix(origin);
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
		return Empty()->pix(origin);
	}

	auto img = prepareBlur(_data);
	if (h <= 0) {
		img = img.scaledToWidth(w, Qt::SmoothTransformation);
	} else {
		img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	}

	return App::pixmapFromImageInPlace(prepareColored(add, img));
}

QImage Image::original() const {
	checkSource();
	return _data;
}

void Image::automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	if (!loaded()) {
		_source->automaticLoad(origin, item);
	}
}

void Image::load(Data::FileOrigin origin) {
	if (!loaded()) {
		_source->load(origin);
	}
}

void Image::loadEvenCancelled(Data::FileOrigin origin) {
	if (!loaded()) {
		_source->loadEvenCancelled(origin);
	}
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
	if (this != Empty() && this != BlankMedia()) {
		unload();
		ActiveCache().remove(this);
	}
}
