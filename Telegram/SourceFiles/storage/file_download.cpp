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
#include "storage/localstorage.h"
#include "platform/platform_file_utilities.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "core/crash_reports.h"
#include "base/bytes.h"
#include "base/openssl_help.h"
#include "facades.h"
#include "app.h"

namespace Storage {
namespace {

// How much time without download causes additional session kill.
constexpr auto kKillSessionTimeout = 15 * crl::time(1000);

// Max 16 file parts downloaded at the same time, 128 KB each.
constexpr auto kMaxFileQueries = 16;

constexpr auto kMaxWaitedInConnection = 512 * 1024;

// Max 8 http[s] files downloaded at the same time.
constexpr auto kMaxWebFileQueries = 8;

constexpr auto kStartSessionsCount = 1;
constexpr auto kMaxSessionsCount = 8;
constexpr auto kResetDownloadPrioritiesTimeout = crl::time(200);

} // namespace

void DownloadManager::Queue::enqueue(not_null<Downloader*> loader) {
	const auto i = ranges::find(_loaders, loader);
	if (i != end(_loaders)) {
		return;
	}
	_loaders.push_back(loader);
	_previousGeneration.erase(
		ranges::remove(_previousGeneration, loader),
		end(_previousGeneration));
}

void DownloadManager::Queue::remove(not_null<Downloader*> loader) {
	_loaders.erase(ranges::remove(_loaders, loader), end(_loaders));
	_previousGeneration.erase(
		ranges::remove(_previousGeneration, loader),
		end(_previousGeneration));
}

void DownloadManager::Queue::resetGeneration() {
	if (!_previousGeneration.empty()) {
		_loaders.reserve(_loaders.size() + _previousGeneration.size());
		std::copy(
			begin(_previousGeneration),
			end(_previousGeneration),
			std::back_inserter(_loaders));
		_previousGeneration.clear();
	}
	std::swap(_loaders, _previousGeneration);
}

bool DownloadManager::Queue::empty() const {
	return _loaders.empty() && _previousGeneration.empty();
}

Downloader *DownloadManager::Queue::nextLoader() const {
	auto &&all = ranges::view::concat(_loaders, _previousGeneration);
	const auto i = ranges::find(all, true, &Downloader::readyToRequest);
	return (i != all.end()) ? i->get() : nullptr;
}

DownloadManager::DownloadManager(not_null<ApiWrap*> api)
: _api(api)
, _resetGenerationTimer([=] { resetGeneration(); })
, _killDownloadSessionsTimer([=] { killDownloadSessions(); }) {
}

DownloadManager::~DownloadManager() {
	killDownloadSessions();
}

void DownloadManager::enqueue(not_null<Downloader*> loader) {
	const auto dcId = loader->dcId();
	(dcId ? _mtprotoLoaders[dcId] : _webLoaders).enqueue(loader);
	if (!_resetGenerationTimer.isActive()) {
		_resetGenerationTimer.callOnce(kResetDownloadPrioritiesTimeout);
	}
	checkSendNext();
}

void DownloadManager::remove(not_null<Downloader*> loader) {
	const auto dcId = loader->dcId();
	(dcId ? _mtprotoLoaders[dcId] : _webLoaders).remove(loader);
	crl::on_main(&_api->session(), [=] { checkSendNext(); });
}

void DownloadManager::resetGeneration() {
	_resetGenerationTimer.cancel();
	for (auto &[dcId, queue] : _mtprotoLoaders) {
		queue.resetGeneration();
	}
	_webLoaders.resetGeneration();
}

void DownloadManager::checkSendNext() {
	for (auto &[dcId, queue] : _mtprotoLoaders) {
		if (queue.empty()) {
			continue;
		}
		const auto bestIndex = [&] {
			const auto i = _requestedBytesAmount.find(dcId);
			if (i == end(_requestedBytesAmount)) {
				_requestedBytesAmount[dcId].resize(kStartSessionsCount);
				return 0;
			}
			const auto j = ranges::min_element(i->second);
			const auto already = *j;
			return (already + kDownloadPartSize <= kMaxWaitedInConnection)
				? (j - begin(i->second))
				: -1;
		}();
		if (bestIndex < 0) {
			continue;
		}
		if (const auto loader = queue.nextLoader()) {
			loader->loadPart(bestIndex);
		}
	}
	if (_requestedBytesAmount[0].empty()) {
		_requestedBytesAmount[0] = std::vector<int>(1, 0);
	}
	if (_requestedBytesAmount[0][0] < kMaxWebFileQueries) {
		if (const auto loader = _webLoaders.nextLoader()) {
			loader->loadPart(0);
		}
	}
}

void DownloadManager::requestedAmountIncrement(
		MTP::DcId dcId,
		int index,
		int amount) {
	using namespace rpl::mappers;

	auto it = _requestedBytesAmount.find(dcId);
	if (it == _requestedBytesAmount.end()) {
		it = _requestedBytesAmount.emplace(
			dcId,
			std::vector<int>(dcId ? kStartSessionsCount : 1, 0)
		).first;
	}
	it->second[index] += amount;
	if (!dcId) {
		return; // webLoaders.
	}
	if (amount > 0) {
		killDownloadSessionsStop(dcId);
	} else if (ranges::find_if(it->second, _1 > 0) == end(it->second)) {
		killDownloadSessionsStart(dcId);
		checkSendNext();
	}
}

int DownloadManager::chooseDcIndexForRequest(MTP::DcId dcId) {
	const auto i = _requestedBytesAmount.find(dcId);
	return (i != end(_requestedBytesAmount))
		? (ranges::min_element(i->second) - begin(i->second))
		: 0;
}

void DownloadManager::killDownloadSessionsStart(MTP::DcId dcId) {
	if (!_killDownloadSessionTimes.contains(dcId)) {
		_killDownloadSessionTimes.emplace(
			dcId,
			crl::now() + kKillSessionTimeout);
	}
	if (!_killDownloadSessionsTimer.isActive()) {
		_killDownloadSessionsTimer.callOnce(kKillSessionTimeout + 5);
	}
}

void DownloadManager::killDownloadSessionsStop(MTP::DcId dcId) {
	_killDownloadSessionTimes.erase(dcId);
	if (_killDownloadSessionTimes.empty()
		&& _killDownloadSessionsTimer.isActive()) {
		_killDownloadSessionsTimer.cancel();
	}
}

void DownloadManager::killDownloadSessions() {
	const auto now = crl::now();
	auto left = kKillSessionTimeout;
	for (auto i = _killDownloadSessionTimes.begin(); i != _killDownloadSessionTimes.end(); ) {
		if (i->second <= now) {
			const auto j = _requestedBytesAmount.find(i->first);
			if (j != end(_requestedBytesAmount)) {
				for (auto index = 0; index != int(j->second.size()); ++index) {
					MTP::stopSession(MTP::downloadDcId(i->first, index));
				}
			}
			i = _killDownloadSessionTimes.erase(i);
		} else {
			if (i->second - now < left) {
				left = i->second - now;
			}
			++i;
		}
	}
	if (!_killDownloadSessionTimes.empty()) {
		_killDownloadSessionsTimer.callOnce(left);
	}
}

} // namespace Storage

FileLoader::FileLoader(
	const QString &toFile,
	int32 size,
	LocationType locationType,
	LoadToCacheSetting toCache,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: _session(&Auth())
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

FileLoader::~FileLoader() = default;

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
	Auth().downloaderTaskFinished().notify();
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
	emit progress(this);
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
	notifyAboutProgress();
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
	session().data().cache().get(key, [=, callback = std::move(done)](
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

	const auto weak = QPointer<FileLoader>(this);
	if (_toCache == LoadToCacheAsWell) {
		loadLocal(cacheKey());
		emit progress(this);
	}
	if (!weak) {
		return false;
	} else if (_localStatus != LocalStatus::NotTried) {
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
	cancelRequests();
	_cancelled = true;
	_finished = true;
	if (_fileIsOpen) {
		_file.close();
		_fileIsOpen = false;
		_file.remove();
	}
	_data = QByteArray();

	const auto weak = QPointer<FileLoader>(this);
	if (fail) {
		emit failed(this, started);
	} else {
		emit progress(this);
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
				Local::writeFileLocation(*key, FileLocation(_filename));
			}
		}
		if ((_toCache == LoadToCacheAsWell)
			&& (_data.size() <= Storage::kMaxFileInMemory)) {
			session().data().cache().put(
				cacheKey(),
				Storage::Cache::Database::TaggedValue(
					base::duplicate(_data),
					_cacheTag));
		}
	}
	Auth().downloaderTaskFinished().notify();
	return true;
}
