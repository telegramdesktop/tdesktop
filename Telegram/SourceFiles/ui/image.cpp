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

namespace Images {
namespace {

QMap<QString, Image*> LocalFileImages;
QMap<QString, Image*> WebUrlImages;
QMap<StorageKey, Image*> StorageImages;
QMap<StorageKey, Image*> WebCachedImages;
QMap<StorageKey, Image*> GeoPointImages;

int64 GlobalAcquiredSize = 0;
int64 LocalAcquiredSize = 0;

uint64 PixKey(int width, int height, Images::Options options) {
	return static_cast<uint64>(width) | (static_cast<uint64>(height) << 24) | (static_cast<uint64>(options) << 48);
}

uint64 SinglePixKey(Images::Options options) {
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
	LocalAcquiredSize = GlobalAcquiredSize;
}

void ClearAll() {
	for (auto image : base::take(LocalFileImages)) {
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

Source::~Source() = default;

ImageSource::ImageSource(QImage &&data, const QByteArray &format)
: _data(std::move(data))
, _format(format) {
}

void ImageSource::load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
}

void ImageSource::loadEvenCancelled(
	Data::FileOrigin origin,
	bool loadFirst,
	bool prior) {
}

QImage ImageSource::takeLoaded() {
	return _data;
}

void ImageSource::forget() {
}

void ImageSource::automaticLoad(
	Data::FileOrigin origin,
	const HistoryItem *item) {
}

void ImageSource::automaticLoadSettingsChanged() {
}

bool ImageSource::loading() {
	return false;
}

bool ImageSource::displayLoading() {
	return false;
}

void ImageSource::cancel() {
}

float64 ImageSource::progress() {
	return 1.;
}

int ImageSource::loadOffset() {
	return 0;
}

const StorageImageLocation &ImageSource::location() {
	return StorageImageLocation::Null;
}

void ImageSource::refreshFileReference(const QByteArray &data) {
}

std::optional<Storage::Cache::Key> ImageSource::cacheKey() {
	return std::nullopt;
}

void ImageSource::setDelayedStorageLocation(
		const StorageImageLocation &location) {
}

void ImageSource::performDelayedLoad(Data::FileOrigin origin) {
}

bool ImageSource::isDelayedStorageImage() const {
	return false;
}

void ImageSource::setImageBytes(const QByteArray &bytes) {
}

int ImageSource::width() {
	return _data.width();
}

int ImageSource::height() {
	return _data.height();
}

void ImageSource::setInformation(int size, int width, int height) {
}

QByteArray ImageSource::bytesForCache() {
	auto result = QByteArray();
	{
		QBuffer buffer(&result);
		if (!_data.save(&buffer, _format)) {
			if (_data.save(&buffer, "PNG")) {
				_format = "PNG";
			}
		}
	}
	return result;
}

LocalFileSource::LocalFileSource(
	const QString &path,
	const QByteArray &content,
	const QByteArray &format,
	QImage &&data)
: _path(path)
, _bytes(content)
, _format(format)
, _data(std::move(data)) {
}

void LocalFileSource::load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	if (!_data.isNull()) {
		return;
	}
	if (_bytes.isEmpty()) {
		QFile f(_path);
		if (f.size() <= App::kImageSizeLimit && f.open(QIODevice::ReadOnly)) {
			_bytes = f.readAll();
		}
		if (_bytes.isEmpty()) {
			_bytes = "(bad)";
		}
	}
	if (_bytes != "(bad)") {
		_data = App::readImage(_bytes, &_format, false, nullptr);
	}
	_width = std::max(_data.width(), 1);
	_height = std::max(_data.height(), 1);
}

void LocalFileSource::loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	load(origin, loadFirst, prior);
}

QImage LocalFileSource::takeLoaded() {
	return std::move(_data);
}

void LocalFileSource::forget() {
	_data = QImage();
}

void LocalFileSource::automaticLoad(
	Data::FileOrigin origin,
	const HistoryItem *item) {
}

void LocalFileSource::automaticLoadSettingsChanged() {
}

bool LocalFileSource::loading() {
	return false;
}

bool LocalFileSource::displayLoading() {
	return false;
}

void LocalFileSource::cancel() {
}

float64 LocalFileSource::progress() {
	return 1.;
}

int LocalFileSource::loadOffset() {
	return 0;
}

const StorageImageLocation &LocalFileSource::location() {
	return StorageImageLocation::Null;
}

void LocalFileSource::refreshFileReference(const QByteArray &data) {
}

std::optional<Storage::Cache::Key> LocalFileSource::cacheKey() {
	return std::nullopt;
}

void LocalFileSource::setDelayedStorageLocation(
	const StorageImageLocation &location) {
}

void LocalFileSource::performDelayedLoad(Data::FileOrigin origin) {
}

bool LocalFileSource::isDelayedStorageImage() const {
	return false;
}

void LocalFileSource::setImageBytes(const QByteArray &bytes) {
	_bytes = bytes;
	load({}, false, true);
}

int LocalFileSource::width() {
	ensureDimensionsKnown();
	return _width;
}

int LocalFileSource::height() {
	ensureDimensionsKnown();
	return _height;
}

void LocalFileSource::setInformation(int size, int width, int height) {
	ensureDimensionsKnown(); // First load _bytes.
	if (width && height) {
		_width = width;
		_height = height;
	}
}

void LocalFileSource::ensureDimensionsKnown() {
	if (!_width || !_height) {
		load({}, false, false);
	}
}

QByteArray LocalFileSource::bytesForCache() {
	ensureDimensionsKnown();
	return (_bytes == "(bad)") ? QByteArray() : _bytes;
}

QImage RemoteSource::takeLoaded() {
	if (!loaderValid() || !_loader->finished()) {
		return QImage();
	}

	auto data = _loader->imageData(shrinkBox());
	if (data.isNull()) {
		destroyLoaderDelayed(CancelledFileLoader);
		return QImage();
	}

	setInformation(_loader->bytes().size(), data.width(), data.height());

	destroyLoaderDelayed();

	return data;
}

bool RemoteSource::loaderValid() const {
	return _loader && _loader != CancelledFileLoader;
}

void RemoteSource::destroyLoaderDelayed(FileLoader *newValue) {
	Expects(loaderValid());

	_loader->stop();
	auto loader = std::unique_ptr<FileLoader>(std::exchange(_loader, newValue));
	Auth().downloader().delayedDestroyLoader(std::move(loader));
}

void RemoteSource::loadLocal() {
	if (loaderValid()) {
		return;
	}

	_loader = createLoader(std::nullopt, LoadFromLocalOnly, true);
	if (_loader) _loader->start();
}

void RemoteSource::setImageBytes(const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return;
	}
	_loader = createLoader({}, LoadFromLocalOnly, true);
	_loader->finishWithBytes(bytes);

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

bool RemoteSource::loading() {
	return loaderValid();
}

void RemoteSource::automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
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

void RemoteSource::automaticLoadSettingsChanged() {
	if (_loader == CancelledFileLoader) {
		_loader = nullptr;
	}
}

void RemoteSource::load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	if (!_loader) {
		_loader = createLoader(origin, LoadFromCloudOrLocal, false);
	}
	if (loaderValid()) {
		_loader->start(loadFirst, prior);
	}
}

void RemoteSource::loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	if (_loader == CancelledFileLoader) {
		_loader = nullptr;
	}
	return load(origin, loadFirst, prior);
}

RemoteSource::~RemoteSource() {
	forget();
}

bool RemoteSource::displayLoading() {
	return loaderValid()
		&& (!_loader->loadingLocal() || !_loader->autoLoading());
}

void RemoteSource::cancel() {
	if (!loaderValid()) return;

	const auto loader = std::exchange(_loader, CancelledFileLoader);
	loader->cancel();
	loader->stop();
	Auth().downloader().delayedDestroyLoader(
		std::unique_ptr<FileLoader>(loader));
}

void RemoteSource::forget() {
	if (loaderValid()) {
		destroyLoaderDelayed();
	}
}

float64 RemoteSource::progress() {
	return loaderValid() ? _loader->currentProgress() : 0.;
}

int RemoteSource::loadOffset() {
	return loaderValid() ? _loader->currentOffset() : 0;
}

const StorageImageLocation &RemoteSource::location() {
	return StorageImageLocation::Null;
}

void RemoteSource::refreshFileReference(const QByteArray &data) {
}

void RemoteSource::setDelayedStorageLocation(
	const StorageImageLocation &location) {
}

void RemoteSource::performDelayedLoad(Data::FileOrigin origin) {
}

bool RemoteSource::isDelayedStorageImage() const {
	return false;
}

QByteArray RemoteSource::bytesForCache() {
	return QByteArray();
}

StorageSource::StorageSource(const StorageImageLocation &location, int size)
: _location(location)
, _size(size) {
}

void StorageSource::refreshFileReference(const QByteArray &data) {
	_location.refreshFileReference(data);
}

const StorageImageLocation &StorageSource::location() {
	return _location;
}

std::optional<Storage::Cache::Key> StorageSource::cacheKey() {
	return _location.isNull()
		? std::nullopt
		: base::make_optional(Data::StorageCacheKey(_location));
}

int StorageSource::width() {
	return _location.width();
}

int StorageSource::height() {
	return _location.height();
}

void StorageSource::setInformation(int size, int width, int height) {
	if (size) {
		_size = size;
	}
	if (width && height) {
		_location.setSize(width, height);
	}
}

QSize StorageSource::shrinkBox() const {
	return QSize();
}

FileLoader *StorageSource::createLoader(
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

WebCachedSource::WebCachedSource(
	const WebFileLocation &location,
	QSize box,
	int size)
: _location(location)
, _box(box)
, _size(size) {
}

WebCachedSource::WebCachedSource(
	const WebFileLocation &location,
	int width,
	int height,
	int size)
: _location(location)
, _width(width)
, _height(height)
, _size(size) {
}

std::optional<Storage::Cache::Key> WebCachedSource::cacheKey() {
	return _location.isNull()
		? std::nullopt
		: base::make_optional(Data::WebDocumentCacheKey(_location));
}

int WebCachedSource::width() {
	return _width;
}

int WebCachedSource::height() {
	return _height;
}

void WebCachedSource::setInformation(int size, int width, int height) {
	if (size) {
		_size = size;
	}
	if (width && height) {
		_width = width;
		_height = height;
	}
}

QSize WebCachedSource::shrinkBox() const {
	return _box;
}

FileLoader *WebCachedSource::createLoader(
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

GeoPointSource::GeoPointSource(const GeoPointLocation &location)
: _location(location) {
}

std::optional<Storage::Cache::Key> GeoPointSource::cacheKey() {
	return Data::GeoPointCacheKey(_location);
}

int GeoPointSource::width() {
	return _location.width * _location.scale;
}

int GeoPointSource::height() {
	return _location.height * _location.scale;
}

void GeoPointSource::setInformation(int size, int width, int height) {
	Expects(_location.scale != 0);

	if (size) {
		_size = size;
	}
	if (width && height) {
		_location.width = width / _location.scale;
		_location.height = height / _location.scale;
	}
}

QSize GeoPointSource::shrinkBox() const {
	return QSize();
}

FileLoader *GeoPointSource::createLoader(
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

DelayedStorageSource::DelayedStorageSource()
: StorageSource(StorageImageLocation(), 0) {
}

DelayedStorageSource::DelayedStorageSource(int w, int h)
: StorageSource(StorageImageLocation(w, h, 0, 0, 0, 0, {}), 0) {
}

void DelayedStorageSource::setDelayedStorageLocation(
		const StorageImageLocation &location) {
	_location = location;
}

void DelayedStorageSource::performDelayedLoad(Data::FileOrigin origin) {
	if (!_loadRequested) {
		return;
	}
	_loadRequested = false;
	if (_loadCancelled) {
		return;
	}
	if (base::take(_loadFromCloud)) {
		load(origin, false, true);
	} else {
		loadLocal();
	}
}

void DelayedStorageSource::automaticLoad(
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
		StorageSource::automaticLoad(origin, item);
	}
}

void DelayedStorageSource::automaticLoadSettingsChanged() {
	if (_loadCancelled) _loadCancelled = false;
	StorageSource::automaticLoadSettingsChanged();
}

void DelayedStorageSource::load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	if (_location.isNull()) {
		_loadRequested = _loadFromCloud = true;
	} else {
		StorageSource::load(origin, loadFirst, prior);
	}
}

void DelayedStorageSource::loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	_loadCancelled = false;
	StorageSource::loadEvenCancelled(origin, loadFirst, prior);
}

bool DelayedStorageSource::displayLoading() {
	return _location.isNull() ? true : StorageSource::displayLoading();
}

void DelayedStorageSource::cancel() {
	if (_loadRequested) {
		_loadRequested = false;
	}
	StorageSource::cancel();
}

bool DelayedStorageSource::isDelayedStorageImage() const {
	return true;
}

WebUrlSource::WebUrlSource(const QString &url, QSize box)
: _url(url)
, _box(box) {
}

WebUrlSource::WebUrlSource(const QString &url, int width, int height)
: _url(url)
, _width(width)
, _height(height) {
}

std::optional<Storage::Cache::Key> WebUrlSource::cacheKey() {
	return Data::UrlCacheKey(_url);
}

int WebUrlSource::width() {
	return _width;
}

int WebUrlSource::height() {
	return _height;
}

void WebUrlSource::setInformation(int size, int width, int height) {
	if (size) {
		_size = size;
	}
	if (width && height) {
		_width = width;
		_height = height;
	}
}

QSize WebUrlSource::shrinkBox() const {
	return _box;
}

FileLoader *WebUrlSource::createLoader(
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

} // namespace Images

Image::Image(std::unique_ptr<Images::Source> &&source)
: _source(std::move(source)) {
}

void Image::replaceSource(std::unique_ptr<Images::Source> &&source) {
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
		return Images::Create(
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
	auto options = Images::Option::Smooth | Images::Option::None;
	auto k = Images::PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
        p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			Images::GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
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
	checkSource();

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
	auto k = Images::PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			Images::GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
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
	auto options = Images::Option::Smooth | Images::Option::Circled;
	auto k = Images::PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			Images::GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
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
	auto options = Images::Option::Smooth | Images::Option::Circled | Images::Option::Blurred;
	auto k = Images::PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			Images::GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
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
	auto options = Images::Option::Smooth | Images::Option::Blurred;
	auto k = Images::PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixNoCache(origin, w, h, options);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			Images::GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
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
	auto options = Images::Option::Smooth | Images::Option::Colored;
	auto k = Images::PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixColoredNoCache(origin, add, w, h, true);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			Images::GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
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
	auto options = Images::Option::Blurred | Images::Option::Smooth | Images::Option::Colored;
	auto k = Images::PixKey(w, h, options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend()) {
		auto p = pixBlurredColoredNoCache(origin, add, w, h);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			Images::GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
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
	checkSource();

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

	auto k = Images::SinglePixKey(options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend() || i->width() != (outerw * cIntRetinaFactor()) || i->height() != (outerh * cIntRetinaFactor())) {
		if (i != _sizesCache.cend()) {
			Images::GlobalAcquiredSize -= int64(i->width()) * i->height() * 4;
		}
		auto p = pixNoCache(origin, w, h, options, outerw, outerh, colored);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			Images::GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
		}
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

	auto k = Images::SinglePixKey(options);
	auto i = _sizesCache.constFind(k);
	if (i == _sizesCache.cend() || i->width() != (outerw * cIntRetinaFactor()) || i->height() != (outerh * cIntRetinaFactor())) {
		if (i != _sizesCache.cend()) {
			Images::GlobalAcquiredSize -= int64(i->width()) * i->height() * 4;
		}
		auto p = pixNoCache(origin, w, h, options, outerw, outerh);
		p.setDevicePixelRatio(cRetinaFactor());
		i = _sizesCache.insert(k, p);
		if (!p.isNull()) {
			Images::GlobalAcquiredSize += int64(p.width()) * p.height() * 4;
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

	return Images::pixmap(_data, w, h, options, outerw, outerh, colored);
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
	if (!loading()) {
		const_cast<Image*>(this)->load(origin);
	}
	checkSource();

	if (_data.isNull()) {
		return Blank()->pix(origin);
	}

	auto img = Images::prepareBlur(_data);
	if (h <= 0) {
		img = img.scaledToWidth(w, Qt::SmoothTransformation);
	} else {
		img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	}

	return App::pixmapFromImageInPlace(Images::prepareColored(add, img));
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
		if (!_data.isNull()) {
			Images::GlobalAcquiredSize += int64(_data.width()) * _data.height() * 4;
		}
	}
}

void Image::forget() const {
	_source->takeLoaded();
	_source->forget();
	invalidateSizeCache();
	if (!_data.isNull()) {
		Images::GlobalAcquiredSize -= int64(_data.width()) * _data.height() * 4;
		_data = QImage();
	}
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
	for (const auto &image : std::as_const(_sizesCache)) {
		if (!image.isNull()) {
			Images::GlobalAcquiredSize -= int64(image.width()) * image.height() * 4;
		}
	}
	_sizesCache.clear();
}

Image::~Image() {
	forget();
}
