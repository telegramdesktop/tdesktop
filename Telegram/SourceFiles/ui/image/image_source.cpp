/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/image_source.h"

#include "storage/file_download.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
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

void ImageSource::load(Data::FileOrigin origin) {
	if (_data.isNull() && !_bytes.isEmpty()) {
		_data = App::readImage(_bytes, &_format, false);
	}
}

void ImageSource::loadEvenCancelled(Data::FileOrigin origin) {
	load(origin);
}

QImage ImageSource::takeLoaded() {
	load({});
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
	return StorageImageLocation::Invalid();
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

void LocalFileSource::load(Data::FileOrigin origin) {
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

void LocalFileSource::loadEvenCancelled(Data::FileOrigin origin) {
	load(origin);
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
	return StorageImageLocation::Invalid();
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
	load({});
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
		load({});
	}
}

QByteArray LocalFileSource::bytesForCache() {
	ensureDimensionsKnown();
	return (_bytes == "(bad)") ? QByteArray() : _bytes;
}

QImage RemoteSource::takeLoaded() {
	if (!_loader || !_loader->finished()) {
		return QImage();
	}

	if (_loader->cancelled()) {
		_cancelled = true;
		destroyLoader();
		return QImage();
	}
	auto data = _loader->imageData(shrinkBox());
	if (data.isNull()) {
		// Bad content in the image.
		data = Image::Empty()->original();
	}

	setInformation(_loader->bytes().size(), data.width(), data.height());

	destroyLoader();

	return data;
}

void RemoteSource::destroyLoader() {
	if (!_loader) {
		return;
	}

	const auto loader = base::take(_loader);
	if (cancelled()) {
		loader->cancel();
	}
	loader->stop();
}

void RemoteSource::loadLocal() {
	if (_loader) {
		return;
	}

	_loader = createLoader(Data::FileOrigin(), LoadFromLocalOnly, true);
	if (_loader) {
		_loader->start();
	}
}

void RemoteSource::setImageBytes(const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return;
	} else if (_loader) {
		unload();
	}
	_loader = createLoader({}, LoadFromLocalOnly, true);
	_loader->finishWithBytes(bytes);

	const auto location = this->location();
	if (location.valid()
		&& !bytes.isEmpty()
		&& bytes.size() <= Storage::kMaxFileInMemory) {
		Auth().data().cache().putIfEmpty(
			location.file().cacheKey(),
			Storage::Cache::Database::TaggedValue(
				base::duplicate(bytes),
				Data::kImageCacheTag));
	}
}

bool RemoteSource::loading() {
	return (_loader != nullptr);
}

void RemoteSource::automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	if (!item || cancelled()) {
		return;
	}
	const auto loadFromCloud = Data::AutoDownload::Should(
		Auth().settings().autoDownload(),
		item->history()->peer,
		this);

	if (_loader) {
		if (loadFromCloud) {
			_loader->permitLoadFromCloud();
		}
	} else {
		_loader = createLoader(
			origin,
			loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly,
			true);
	}
	if (_loader) {
		_loader->start();
	}
}

void RemoteSource::automaticLoadSettingsChanged() {
	_cancelled = false;
}

void RemoteSource::load(Data::FileOrigin origin) {
	if (!_loader) {
		_loader = createLoader(origin, LoadFromCloudOrLocal, false);
	}
	if (_loader) {
		_loader->start();
	}
}

bool RemoteSource::cancelled() const {
	return _cancelled;
}

void RemoteSource::loadEvenCancelled(Data::FileOrigin origin) {
	_cancelled = false;
	return load(origin);
}

bool RemoteSource::displayLoading() {
	return _loader && (!_loader->loadingLocal() || !_loader->autoLoading());
}

void RemoteSource::cancel() {
	if (!_loader) {
		return;
	}
	_cancelled = true;
	destroyLoader();
}

void RemoteSource::unload() {
	base::take(_loader);
}

float64 RemoteSource::progress() {
	return _loader ? _loader->currentProgress() : 0.;
}

int RemoteSource::loadOffset() {
	return _loader ? _loader->currentOffset() : 0;
}

RemoteSource::~RemoteSource() {
	unload();
}

const StorageImageLocation &RemoteSource::location() {
	return StorageImageLocation::Invalid();
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
	return _location.valid()
		? base::make_optional(_location.file().cacheKey())
		: std::nullopt;
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

std::unique_ptr<FileLoader> StorageSource::createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	return _location.valid()
		? std::make_unique<mtpFileLoader>(
			_location.file(),
			origin,
			UnknownFileLocation,
			QString(),
			_size,
			LoadToCacheAsWell,
			fromCloud,
			autoLoading,
			Data::kImageCacheTag)
		: nullptr;
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

std::unique_ptr<FileLoader> WebCachedSource::createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	return !_location.isNull()
		? std::make_unique<mtpFileLoader>(
			_location,
			_size,
			fromCloud,
			autoLoading,
			Data::kImageCacheTag)
		: nullptr;
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

std::unique_ptr<FileLoader> GeoPointSource::createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	return std::make_unique<mtpFileLoader>(
		_location,
		_size,
		fromCloud,
		autoLoading,
		Data::kImageCacheTag);
}

DelayedStorageSource::DelayedStorageSource()
: StorageSource(StorageImageLocation(), 0) {
}

DelayedStorageSource::DelayedStorageSource(int w, int h)
: StorageSource(StorageImageLocation(StorageFileLocation(), w, h), 0) {
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
		load(origin);
	} else {
		loadLocal();
	}
}

void DelayedStorageSource::automaticLoad(
		Data::FileOrigin origin,
		const HistoryItem *item) {
	if (_location.valid()) {
		StorageSource::automaticLoad(origin, item);
		return;
	} else if (_loadCancelled || !item) {
		return;
	}
	const auto loadFromCloud = Data::AutoDownload::Should(
		Auth().settings().autoDownload(),
		item->history()->peer,
		this);

	if (_loadRequested) {
		if (loadFromCloud) _loadFromCloud = loadFromCloud;
	} else {
		_loadFromCloud = loadFromCloud;
		_loadRequested = true;
	}
}

void DelayedStorageSource::automaticLoadSettingsChanged() {
	if (_loadCancelled) _loadCancelled = false;
	StorageSource::automaticLoadSettingsChanged();
}

void DelayedStorageSource::load(Data::FileOrigin origin) {
	if (_location.valid()) {
		StorageSource::load(origin);
	} else {
		_loadRequested = _loadFromCloud = true;
	}
}

void DelayedStorageSource::loadEvenCancelled(Data::FileOrigin origin) {
	_loadCancelled = false;
	StorageSource::loadEvenCancelled(origin);
}

bool DelayedStorageSource::displayLoading() {
	return _location.valid() ? StorageSource::displayLoading() : true;
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

std::unique_ptr<FileLoader> WebUrlSource::createLoader(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	return std::make_unique<webFileLoader>(
		_url,
		QString(),
		fromCloud,
		autoLoading,
		Data::kImageCacheTag);
}

} // namespace Images
