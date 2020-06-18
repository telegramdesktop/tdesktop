/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/file_download.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "core/application.h"
#include "storage/storage_account.h"
#include "storage/file_download_mtproto.h"
#include "storage/file_download_web.h"
#include "platform/platform_file_utilities.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "core/crash_reports.h"
#include "base/bytes.h"
#include "base/openssl_help.h"
#include "app.h"

namespace {

class FromMemoryLoader final : public FileLoader {
public:
	FromMemoryLoader(
		not_null<Main::Session*> session,
		const QByteArray &data,
		const QString &toFile,
		int32 size,
		LocationType locationType,
		LoadToCacheSetting toCache,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);

private:
	Storage::Cache::Key cacheKey() const override;
	std::optional<MediaKey> fileLocationKey() const override;
	void cancelHook() override;
	void startLoading() override;

	QByteArray _data;

};

FromMemoryLoader::FromMemoryLoader(
	not_null<Main::Session*> session,
	const QByteArray &data,
	const QString &toFile,
	int32 size,
	LocationType locationType,
	LoadToCacheSetting toCache,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag
) : FileLoader(
	session,
	toFile,
	size,
	locationType,
	toCache,
	fromCloud,
	autoLoading,
	cacheTag)
, _data(data) {
}

Storage::Cache::Key FromMemoryLoader::cacheKey() const {
	return {};
}

std::optional<MediaKey> FromMemoryLoader::fileLocationKey() const {
	return std::nullopt;
}

void FromMemoryLoader::cancelHook() {
}

void FromMemoryLoader::startLoading() {
	finishWithBytes(_data);
}

} // namespace

FileLoader::FileLoader(
	not_null<Main::Session*> session,
	const QString &toFile,
	int32 size,
	LocationType locationType,
	LoadToCacheSetting toCache,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: _session(session)
, _autoLoading(autoLoading)
, _cacheTag(cacheTag)
, _filename(toFile)
, _file(_filename)
, _toCache(toCache)
, _fromCloud(fromCloud)
, _size(size)
, _locationType(locationType) {
	Expects(!_filename.isEmpty() || (_size <= Storage::kMaxFileInMemory));
}

FileLoader::~FileLoader() {
	Expects(_finished);
}

Main::Session &FileLoader::session() const {
	return *_session;
}

void FileLoader::finishWithBytes(const QByteArray &data) {
	_data = data;
	_localStatus = LocalStatus::Loaded;
	if (!_filename.isEmpty() && _toCache == LoadToCacheAsWell) {
		if (!_fileIsOpen) _fileIsOpen = _file.open(QIODevice::WriteOnly);
		if (!_fileIsOpen) {
			cancel(true);
			return;
		}
		_file.seek(0);
		if (_file.write(_data) != qint64(_data.size())) {
			cancel(true);
			return;
		}
	}

	_finished = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		Platform::File::PostprocessDownloaded(
			QFileInfo(_file).absoluteFilePath());
	}
	_session->downloaderTaskFinished().notify();
	_updates.fire_done();
}

QByteArray FileLoader::imageFormat(const QSize &shrinkBox) const {
	if (_imageFormat.isEmpty() && _locationType == UnknownFileLocation) {
		readImage(shrinkBox);
	}
	return _imageFormat;
}

QImage FileLoader::imageData(const QSize &shrinkBox) const {
	if (_imageData.isNull() && _locationType == UnknownFileLocation) {
		readImage(shrinkBox);
	}
	return _imageData;
}

void FileLoader::readImage(const QSize &shrinkBox) const {
	auto format = QByteArray();
	auto image = App::readImage(_data, &format, false);
	if (!image.isNull()) {
		if (!shrinkBox.isEmpty() && (image.width() > shrinkBox.width() || image.height() > shrinkBox.height())) {
			_imageData = image.scaled(shrinkBox, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		} else {
			_imageData = std::move(image);
		}
		_imageFormat = format;
	}
}

Data::FileOrigin FileLoader::fileOrigin() const {
	return Data::FileOrigin();
}

float64 FileLoader::currentProgress() const {
	if (_finished) return 1.;
	if (!fullSize()) return 0.;
	return snap(float64(currentOffset()) / fullSize(), 0., 1.);
}

int FileLoader::fullSize() const {
	return _size;
}

bool FileLoader::setFileName(const QString &fileName) {
	if (_toCache != LoadToCacheAsWell || !_filename.isEmpty()) {
		return fileName.isEmpty() || (fileName == _filename);
	}
	_filename = fileName;
	_file.setFileName(_filename);
	return true;
}

void FileLoader::permitLoadFromCloud() {
	_fromCloud = LoadFromCloudOrLocal;
}

void FileLoader::notifyAboutProgress() {
	_updates.fire({});
}

void FileLoader::localLoaded(
		const StorageImageSaved &result,
		const QByteArray &imageFormat,
		const QImage &imageData) {
	_localLoading = nullptr;
	if (result.data.isEmpty()) {
		_localStatus = LocalStatus::NotFound;
		start();
		return;
	}
	if (!imageData.isNull()) {
		_imageFormat = imageFormat;
		_imageData = imageData;
	}
	finishWithBytes(result.data);
}

void FileLoader::start() {
	if (_finished || tryLoadLocal()) {
		return;
	} else if (_fromCloud == LoadFromLocalOnly) {
		cancel();
		return;
	}

	if (!_filename.isEmpty() && _toCache == LoadToFileOnly && !_fileIsOpen) {
		_fileIsOpen = _file.open(QIODevice::WriteOnly);
		if (!_fileIsOpen) {
			return cancel(true);
		}
	}
	startLoading();
}

void FileLoader::loadLocal(const Storage::Cache::Key &key) {
	const auto readImage = (_locationType != AudioFileLocation);
	auto done = [=, guard = _localLoading.make_guard()](
			QByteArray &&value,
			QImage &&image,
			QByteArray &&format) mutable {
		crl::on_main(std::move(guard), [
			=,
			value = std::move(value),
			image = std::move(image),
			format = std::move(format)
		]() mutable {
			localLoaded(
				StorageImageSaved(std::move(value)),
				format,
				std::move(image));
		});
	};
	_session->data().cache().get(key, [=, callback = std::move(done)](
			QByteArray &&value) mutable {
		if (readImage) {
			crl::async([
				value = std::move(value),
				done = std::move(callback)
			]() mutable {
				auto format = QByteArray();
				auto image = App::readImage(value, &format, false);
				if (!image.isNull()) {
					done(
						std::move(value),
						std::move(image),
						std::move(format));
				} else {
					done(std::move(value), {}, {});
				}
			});
		} else {
			callback(std::move(value), {}, {});
		}
	});
}

bool FileLoader::tryLoadLocal() {
	if (_localStatus == LocalStatus::NotFound
		|| _localStatus == LocalStatus::Loaded) {
		return false;
	} else if (_localStatus == LocalStatus::Loading) {
		return true;
	}

	if (_toCache == LoadToCacheAsWell) {
		const auto key = cacheKey();
		if (key.low || key.high) {
			loadLocal(key);
			notifyAboutProgress();
		}
	}
	if (_localStatus != LocalStatus::NotTried) {
		return _finished;
	} else if (_localLoading) {
		_localStatus = LocalStatus::Loading;
		return true;
	}
	_localStatus = LocalStatus::NotFound;
	return false;
}

void FileLoader::cancel() {
	cancel(false);
}

void FileLoader::cancel(bool fail) {
	const auto started = (currentOffset() > 0);

	cancelHook();

	_cancelled = true;
	_finished = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		_file.remove();
	}
	_data = QByteArray();

	const auto weak = base::make_weak(this);
	if (fail) {
		_updates.fire_error_copy(started);
	} else {
		_updates.fire_done();
	}
	if (weak) {
		_filename = QString();
		_file.setFileName(_filename);
	}
}

int FileLoader::currentOffset() const {
	return (_fileIsOpen ? _file.size() : _data.size()) - _skippedBytes;
}

bool FileLoader::writeResultPart(int offset, bytes::const_span buffer) {
	Expects(!_finished);

	if (buffer.empty()) {
		return true;
	}
	if (_fileIsOpen) {
		auto fsize = _file.size();
		if (offset < fsize) {
			_skippedBytes -= buffer.size();
		} else if (offset > fsize) {
			_skippedBytes += offset - fsize;
		}
		_file.seek(offset);
		if (_file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size()) != qint64(buffer.size())) {
			cancel(true);
			return false;
		}
		return true;
	}
	_data.reserve(offset + buffer.size());
	if (offset > _data.size()) {
		_skippedBytes += offset - _data.size();
		_data.resize(offset);
	}
	if (offset == _data.size()) {
		_data.append(reinterpret_cast<const char*>(buffer.data()), buffer.size());
	} else {
		_skippedBytes -= buffer.size();
		if (int64(offset + buffer.size()) > _data.size()) {
			_data.resize(offset + buffer.size());
		}
		const auto dst = bytes::make_detached_span(_data).subspan(
			offset,
			buffer.size());
		bytes::copy(dst, buffer);
	}
	return true;
}

QByteArray FileLoader::readLoadedPartBack(int offset, int size) {
	Expects(offset >= 0 && size > 0);

	if (_fileIsOpen) {
		if (_file.openMode() == QIODevice::WriteOnly) {
			_file.close();
			_fileIsOpen = _file.open(QIODevice::ReadWrite);
			if (!_fileIsOpen) {
				cancel(true);
				return QByteArray();
			}
		}
		if (!_file.seek(offset)) {
			return QByteArray();
		}
		auto result = _file.read(size);
		return (result.size() == size) ? result : QByteArray();
	}
	return (offset + size <= _data.size())
		? _data.mid(offset, size)
		: QByteArray();
}

bool FileLoader::finalizeResult() {
	Expects(!_finished);

	if (!_filename.isEmpty() && (_toCache == LoadToCacheAsWell)) {
		if (!_fileIsOpen) {
			_fileIsOpen = _file.open(QIODevice::WriteOnly);
		}
		_file.seek(0);
		if (!_fileIsOpen || _file.write(_data) != qint64(_data.size())) {
			cancel(true);
			return false;
		}
	}

	_finished = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		Platform::File::PostprocessDownloaded(
			QFileInfo(_file).absoluteFilePath());
	}
	if (_localStatus == LocalStatus::NotFound) {
		if (const auto key = fileLocationKey()) {
			if (!_filename.isEmpty()) {
				_session->local().writeFileLocation(
					*key,
					FileLocation(_filename));
			}
		}
		const auto key = cacheKey();
		if ((_toCache == LoadToCacheAsWell)
			&& (_data.size() <= Storage::kMaxFileInMemory)
			&& (key.low || key.high)) {
			_session->data().cache().put(
				cacheKey(),
				Storage::Cache::Database::TaggedValue(
					base::duplicate(_data),
					_cacheTag));
		}
	}
	_session->downloaderTaskFinished().notify();
	_updates.fire_done();
	return true;
}

std::unique_ptr<FileLoader> CreateFileLoader(
		not_null<Main::Session*> session,
		const DownloadLocation &location,
		Data::FileOrigin origin,
		const QString &toFile,
		int size,
		LocationType locationType,
		LoadToCacheSetting toCache,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag) {
	auto result = std::unique_ptr<FileLoader>();
	location.data.match([&](const StorageFileLocation &data) {
		result = std::make_unique<mtpFileLoader>(
			session,
			data,
			origin,
			locationType,
			toFile,
			size,
			toCache,
			fromCloud,
			autoLoading,
			cacheTag);
	}, [&](const WebFileLocation &data) {
		result = std::make_unique<mtpFileLoader>(
			session,
			data,
			size,
			fromCloud,
			autoLoading,
			cacheTag);
	}, [&](const GeoPointLocation &data) {
		result = std::make_unique<mtpFileLoader>(
			session,
			data,
			size,
			fromCloud,
			autoLoading,
			cacheTag);
	}, [&](const PlainUrlLocation &data) {
		result = std::make_unique<webFileLoader>(
			session,
			data.url,
			toFile,
			fromCloud,
			autoLoading,
			cacheTag);
	}, [&](const InMemoryLocation &data) {
		result = std::make_unique<FromMemoryLoader>(
			session,
			data.bytes,
			toFile,
			size,
			locationType,
			toCache,
			LoadFromCloudOrLocal,
			autoLoading,
			cacheTag);
	});

	Ensures(result != nullptr);
	return result;
}
