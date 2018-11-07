/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/image_source.h"

#include "storage/file_download.h"
#include "data/data_session.h"
#include "storage/cache/storage_cache_database.h"
#include "history/history_item.h"
#include "history/history.h"
#include "auth_session.h"

namespace Images {

ImageSource::ImageSource(QImage &&data, const QByteArray &format)
: _data(std::move(data))
, _format(format)
, _width(_data.width())
, _height(_data.height()) {
}

void ImageSource::load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	if (_data.isNull() && !_bytes.isEmpty()) {
		_data = App::readImage(_bytes, &_format, false);
	}
}

void ImageSource::loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	load(origin, loadFirst, prior);
}

QImage ImageSource::takeLoaded() {
	return _data;
}

void ImageSource::unload() {
	if (_bytes.isEmpty() && !_data.isNull()) {
		if (_format != "JPG") {
			_format = "PNG";
		}
		{
			QBuffer buffer(&_bytes);
			_data.save(&buffer, _format);
		}
		Assert(!_bytes.isEmpty());
	}
	_data = QImage();
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
	return _width;
}

int ImageSource::height() {
	return _height;
}

int ImageSource::bytesSize() {
	return _bytes.size();
}

void ImageSource::setInformation(int size, int width, int height) {
	if (width && height) {
		_width = width;
		_height = height;
	}
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
, _data(std::move(data))
, _width(_data.width())
, _height(_data.height()) {
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

void LocalFileSource::unload() {
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

int LocalFileSource::bytesSize() {
	ensureDimensionsKnown();
	return _bytes.size();
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

void RemoteSource::unload() {
	if (loaderValid()) {
		delete base::take(_loader);
	}
}

float64 RemoteSource::progress() {
	return loaderValid() ? _loader->currentProgress() : 0.;
}

int RemoteSource::loadOffset() {
	return loaderValid() ? _loader->currentOffset() : 0;
}

RemoteSource::~RemoteSource() {
	unload();
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

int StorageSource::bytesSize() {
	return _size;
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

int WebCachedSource::bytesSize() {
	return _size;
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

int GeoPointSource::bytesSize() {
	return _size;
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

int WebUrlSource::bytesSize() {
	return _size;
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
