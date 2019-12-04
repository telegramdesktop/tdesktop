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

// Different part sizes are not supported for now :(
// Because we start downloading with some part size
// and then we get a cdn-redirect where we support only
// fixed part size download for hash checking.
constexpr auto kPartSize = 128 * 1024;

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
	const auto i = ranges::find(all, true, &FileLoader::readyToRequest);
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
			const auto inConnection = *j;
			return (inConnection + kPartSize <= kMaxWaitedInConnection)
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

namespace {

QThread *_webLoadThread = nullptr;
WebLoadManager *_webLoadManager = nullptr;
WebLoadManager *webLoadManager() {
	return (_webLoadManager && _webLoadManager != FinishedWebLoadManager) ? _webLoadManager : nullptr;
}
WebLoadMainManager *_webLoadMainManager = nullptr;

} // namespace

FileLoader::FileLoader(
	const QString &toFile,
	MTP::DcId dcId,
	int32 size,
	LocationType locationType,
	LoadToCacheSetting toCache,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: _dcId(dcId)
, _downloader(&Auth().downloader())
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
	_downloader->remove(this);
}

Main::Session &FileLoader::session() const {
	return _downloader->api().session();
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
	_downloader->taskFinished().notify();
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
	_downloader->enqueue(this);
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
	_downloader->remove(this);

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
	_downloader->taskFinished().notify();
	return true;
}

mtpFileLoader::mtpFileLoader(
	const StorageFileLocation &location,
	Data::FileOrigin origin,
	LocationType type,
	const QString &to,
	int32 size,
	LoadToCacheSetting toCache,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	to,
	location.dcId(),
	size,
	type,
	toCache,
	fromCloud,
	autoLoading,
	cacheTag)
, _location(location)
, _origin(origin) {
}

mtpFileLoader::mtpFileLoader(
	const WebFileLocation &location,
	int32 size,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	QString(),
	Global::WebFileDcId(),
	size,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, _location(location) {
}

mtpFileLoader::mtpFileLoader(
	const GeoPointLocation &location,
	int32 size,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	QString(),
	Global::WebFileDcId(),
	size,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, _location(location) {
}

mtpFileLoader::~mtpFileLoader() {
	cancelRequests();
}

Data::FileOrigin mtpFileLoader::fileOrigin() const {
	return _origin;
}

uint64 mtpFileLoader::objId() const {
	if (const auto storage = base::get_if<StorageFileLocation>(&_location)) {
		return storage->objectId();
	}
	return 0;
}

void mtpFileLoader::refreshFileReferenceFrom(
		const Data::UpdatedFileReferences &updates,
		int requestId,
		const QByteArray &current) {
	if (const auto storage = base::get_if<StorageFileLocation>(&_location)) {
		storage->refreshFileReference(updates);
		if (storage->fileReference() == current) {
			cancel(true);
			return;
		}
	} else {
		cancel(true);
		return;
	}
	makeRequest(finishSentRequest(requestId));
}

bool mtpFileLoader::readyToRequest() const {
	return !_finished
		&& !_lastComplete
		&& (_sentRequests.empty() || _size != 0)
		&& (!_size || _nextRequestOffset < _size);
}

void mtpFileLoader::loadPart(int dcIndex) {
	Expects(readyToRequest());

	makeRequest({ _nextRequestOffset, dcIndex });
	_nextRequestOffset += Storage::kPartSize;
}

mtpRequestId mtpFileLoader::sendRequest(const RequestData &requestData) {
	const auto offset = requestData.offset;
	const auto limit = Storage::kPartSize;
	const auto shiftedDcId = MTP::downloadDcId(
		_cdnDcId ? _cdnDcId : dcId(),
		requestData.dcIndex);
	if (_cdnDcId) {
		return MTP::send(
			MTPupload_GetCdnFile(
				MTP_bytes(_cdnToken),
				MTP_int(offset),
				MTP_int(limit)),
			rpcDone(&mtpFileLoader::cdnPartLoaded),
			rpcFail(&mtpFileLoader::cdnPartFailed),
			shiftedDcId,
			50);
	}
	return _location.match([&](const WebFileLocation &location) {
		return MTP::send(
			MTPupload_GetWebFile(
				MTP_inputWebFileLocation(
					MTP_bytes(location.url()),
					MTP_long(location.accessHash())),
				MTP_int(offset),
				MTP_int(limit)),
			rpcDone(&mtpFileLoader::webPartLoaded),
			rpcFail(&mtpFileLoader::partFailed),
			shiftedDcId,
			50);
	}, [&](const GeoPointLocation &location) {
		return MTP::send(
			MTPupload_GetWebFile(
				MTP_inputWebFileGeoPointLocation(
					MTP_inputGeoPoint(
						MTP_double(location.lat),
						MTP_double(location.lon)),
					MTP_long(location.access),
					MTP_int(location.width),
					MTP_int(location.height),
					MTP_int(location.zoom),
					MTP_int(location.scale)),
				MTP_int(offset),
				MTP_int(limit)),
			rpcDone(&mtpFileLoader::webPartLoaded),
			rpcFail(&mtpFileLoader::partFailed),
			shiftedDcId,
			50);
	}, [&](const StorageFileLocation &location) {
		return MTP::send(
			MTPupload_GetFile(
				MTP_flags(0),
				location.tl(session().userId()),
				MTP_int(offset),
				MTP_int(limit)),
			rpcDone(&mtpFileLoader::normalPartLoaded),
			rpcFail(
				&mtpFileLoader::normalPartFailed,
				location.fileReference()),
			shiftedDcId,
			50);
	});
}

void mtpFileLoader::makeRequest(const RequestData &requestData) {
	Expects(!_finished);

	placeSentRequest(sendRequest(requestData), requestData);
}

void mtpFileLoader::requestMoreCdnFileHashes() {
	Expects(!_finished);

	if (_cdnHashesRequestId || _cdnUncheckedParts.empty()) {
		return;
	}

	const auto requestData = _cdnUncheckedParts.cbegin()->first;
	const auto shiftedDcId = MTP::downloadDcId(
		dcId(),
		requestData.dcIndex);
	const auto requestId = _cdnHashesRequestId = MTP::send(
		MTPupload_GetCdnFileHashes(
			MTP_bytes(_cdnToken),
			MTP_int(requestData.offset)),
		rpcDone(&mtpFileLoader::getCdnFileHashesDone),
		rpcFail(&mtpFileLoader::cdnPartFailed),
		shiftedDcId);
	placeSentRequest(requestId, requestData);
}

void mtpFileLoader::normalPartLoaded(
		const MTPupload_File &result,
		mtpRequestId requestId) {
	Expects(!_finished);

	const auto requestData = finishSentRequest(requestId);
	result.match([&](const MTPDupload_fileCdnRedirect &data) {
		switchToCDN(requestData, data);
	}, [&](const MTPDupload_file &data) {
		partLoaded(requestData.offset, bytes::make_span(data.vbytes().v));
	});
}

void mtpFileLoader::webPartLoaded(
		const MTPupload_WebFile &result,
		mtpRequestId requestId) {
	result.match([&](const MTPDupload_webFile &data) {
		const auto requestData = finishSentRequest(requestId);
		if (!_size) {
			_size = data.vsize().v;
		} else if (data.vsize().v != _size) {
			LOG(("MTP Error: "
				"Bad size provided by bot for webDocument: %1, real: %2"
				).arg(_size
				).arg(data.vsize().v));
			cancel(true);
			return;
		}
		partLoaded(requestData.offset, bytes::make_span(data.vbytes().v));
	});
}

void mtpFileLoader::cdnPartLoaded(const MTPupload_CdnFile &result, mtpRequestId requestId) {
	Expects(!_finished);

	const auto requestData = finishSentRequest(requestId);
	result.match([&](const MTPDupload_cdnFileReuploadNeeded &data) {
		const auto shiftedDcId = MTP::downloadDcId(
			dcId(),
			requestData.dcIndex);
		const auto requestId = MTP::send(
			MTPupload_ReuploadCdnFile(
				MTP_bytes(_cdnToken),
				data.vrequest_token()),
			rpcDone(&mtpFileLoader::reuploadDone),
			rpcFail(&mtpFileLoader::cdnPartFailed),
			shiftedDcId);
		placeSentRequest(requestId, requestData);
	}, [&](const MTPDupload_cdnFile &data) {
		auto key = bytes::make_span(_cdnEncryptionKey);
		auto iv = bytes::make_span(_cdnEncryptionIV);
		Expects(key.size() == MTP::CTRState::KeySize);
		Expects(iv.size() == MTP::CTRState::IvecSize);

		auto state = MTP::CTRState();
		auto ivec = bytes::make_span(state.ivec);
		std::copy(iv.begin(), iv.end(), ivec.begin());

		auto counterOffset = static_cast<uint32>(requestData.offset) >> 4;
		state.ivec[15] = static_cast<uchar>(counterOffset & 0xFF);
		state.ivec[14] = static_cast<uchar>((counterOffset >> 8) & 0xFF);
		state.ivec[13] = static_cast<uchar>((counterOffset >> 16) & 0xFF);
		state.ivec[12] = static_cast<uchar>((counterOffset >> 24) & 0xFF);

		auto decryptInPlace = data.vbytes().v;
		auto buffer = bytes::make_detached_span(decryptInPlace);
		MTP::aesCtrEncrypt(buffer, key.data(), &state);

		switch (checkCdnFileHash(requestData.offset, buffer)) {
		case CheckCdnHashResult::NoHash: {
			_cdnUncheckedParts.emplace(requestData, decryptInPlace);
			requestMoreCdnFileHashes();
		} return;

		case CheckCdnHashResult::Invalid: {
			LOG(("API Error: Wrong cdnFileHash for offset %1."
				).arg(requestData.offset));
			cancel(true);
		} return;

		case CheckCdnHashResult::Good: {
			partLoaded(requestData.offset, buffer);
		} return;
		}
		Unexpected("Result of checkCdnFileHash()");
	});
}

mtpFileLoader::CheckCdnHashResult mtpFileLoader::checkCdnFileHash(
		int offset,
		bytes::const_span buffer) {
	const auto cdnFileHashIt = _cdnFileHashes.find(offset);
	if (cdnFileHashIt == _cdnFileHashes.cend()) {
		return CheckCdnHashResult::NoHash;
	}
	const auto realHash = openssl::Sha256(buffer);
	const auto receivedHash = bytes::make_span(cdnFileHashIt->second.hash);
	if (bytes::compare(realHash, receivedHash)) {
		return CheckCdnHashResult::Invalid;
	}
	return CheckCdnHashResult::Good;
}

void mtpFileLoader::reuploadDone(
		const MTPVector<MTPFileHash> &result,
		mtpRequestId requestId) {
	const auto requestData = finishSentRequest(requestId);
	addCdnHashes(result.v);
	makeRequest(requestData);
}

void mtpFileLoader::getCdnFileHashesDone(
		const MTPVector<MTPFileHash> &result,
		mtpRequestId requestId) {
	Expects(!_finished);
	Expects(_cdnHashesRequestId == requestId);

	_cdnHashesRequestId = 0;

	const auto requestData = finishSentRequest(requestId);
	addCdnHashes(result.v);
	auto someMoreChecked = false;
	for (auto i = _cdnUncheckedParts.begin(); i != _cdnUncheckedParts.cend();) {
		const auto uncheckedData = i->first;
		const auto uncheckedBytes = bytes::make_span(i->second);

		switch (checkCdnFileHash(uncheckedData.offset, uncheckedBytes)) {
		case CheckCdnHashResult::NoHash: {
			++i;
		} break;

		case CheckCdnHashResult::Invalid: {
			LOG(("API Error: Wrong cdnFileHash for offset %1."
				).arg(uncheckedData.offset));
			cancel(true);
			return;
		} break;

		case CheckCdnHashResult::Good: {
			someMoreChecked = true;
			const auto goodOffset = uncheckedData.offset;
			const auto goodBytes = std::move(i->second);
			const auto weak = QPointer<mtpFileLoader>(this);
			i = _cdnUncheckedParts.erase(i);
			if (!feedPart(goodOffset, bytes::make_span(goodBytes))
				|| !weak) {
				return;
			} else if (_finished) {
				notifyAboutProgress();
				return;
			}
		} break;

		default: Unexpected("Result of checkCdnFileHash()");
		}
	}
	if (someMoreChecked) {
		const auto weak = QPointer<mtpFileLoader>(this);
		notifyAboutProgress();
		if (weak) {
			requestMoreCdnFileHashes();
		}
		return;
	}
	LOG(("API Error: "
		"Could not find cdnFileHash for offset %1 "
		"after getCdnFileHashes request."
		).arg(requestData.offset));
	cancel(true);
}

void mtpFileLoader::placeSentRequest(
		mtpRequestId requestId,
		const RequestData &requestData) {
	Expects(!_finished);

	_downloader->requestedAmountIncrement(
		dcId(),
		requestData.dcIndex,
		Storage::kPartSize);
	_sentRequests.emplace(requestId, requestData);
}

auto mtpFileLoader::finishSentRequest(mtpRequestId requestId)
-> RequestData {
	auto it = _sentRequests.find(requestId);
	Assert(it != _sentRequests.cend());

	const auto result = it->second;
	_downloader->requestedAmountIncrement(
		dcId(),
		result.dcIndex,
		-Storage::kPartSize);
	_sentRequests.erase(it);

	return result;
}

bool mtpFileLoader::feedPart(int offset, bytes::const_span buffer) {
	if (!writeResultPart(offset, buffer)) {
		return false;
	}
	if (buffer.empty() || (buffer.size() % 1024)) { // bad next offset
		_lastComplete = true;
	}
	const auto finished = _sentRequests.empty()
		&& _cdnUncheckedParts.empty()
		&& (_lastComplete || (_size && _nextRequestOffset >= _size));
	if (finished && !finalizeResult()) {
		return false;
	}
	return true;
}

void mtpFileLoader::partLoaded(int offset, bytes::const_span buffer) {
	if (feedPart(offset, buffer)) {
		notifyAboutProgress();
	}
}

bool mtpFileLoader::normalPartFailed(
		QByteArray fileReference,
		const RPCError &error,
		mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}
	if (error.code() == 400
		&& error.type().startsWith(qstr("FILE_REFERENCE_"))) {
		session().api().refreshFileReference(
			_origin,
			this,
			requestId,
			fileReference);
		return true;
	}
	return partFailed(error, requestId);
}


bool mtpFileLoader::partFailed(
		const RPCError &error,
		mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}
	cancel(true);
	return true;
}

bool mtpFileLoader::cdnPartFailed(
		const RPCError &error,
		mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}

	if (requestId == _cdnHashesRequestId) {
		_cdnHashesRequestId = 0;
	}
	if (error.type() == qstr("FILE_TOKEN_INVALID")
		|| error.type() == qstr("REQUEST_TOKEN_INVALID")) {
		const auto requestData = finishSentRequest(requestId);
		changeCDNParams(
			requestData,
			0,
			QByteArray(),
			QByteArray(),
			QByteArray(),
			QVector<MTPFileHash>());
		return true;
	}
	return partFailed(error, requestId);
}

void mtpFileLoader::cancelRequests() {
	while (!_sentRequests.empty()) {
		auto requestId = _sentRequests.begin()->first;
		MTP::cancel(requestId);
		[[maybe_unused]] const auto data = finishSentRequest(requestId);
	}
}

void mtpFileLoader::switchToCDN(
		const RequestData &requestData,
		const MTPDupload_fileCdnRedirect &redirect) {
	changeCDNParams(
		requestData,
		redirect.vdc_id().v,
		redirect.vfile_token().v,
		redirect.vencryption_key().v,
		redirect.vencryption_iv().v,
		redirect.vfile_hashes().v);
}

void mtpFileLoader::addCdnHashes(const QVector<MTPFileHash> &hashes) {
	for (const auto &hash : hashes) {
		hash.match([&](const MTPDfileHash &data) {
			_cdnFileHashes.emplace(
				data.voffset().v,
				CdnFileHash{ data.vlimit().v, data.vhash().v });
		});
	}
}

void mtpFileLoader::changeCDNParams(
		const RequestData &requestData,
		MTP::DcId dcId,
		const QByteArray &token,
		const QByteArray &encryptionKey,
		const QByteArray &encryptionIV,
		const QVector<MTPFileHash> &hashes) {
	if (dcId != 0
		&& (encryptionKey.size() != MTP::CTRState::KeySize
			|| encryptionIV.size() != MTP::CTRState::IvecSize)) {
		LOG(("Message Error: Wrong key (%1) / iv (%2) size in CDN params").arg(encryptionKey.size()).arg(encryptionIV.size()));
		cancel(true);
		return;
	}

	auto resendAllRequests = (_cdnDcId != dcId
		|| _cdnToken != token
		|| _cdnEncryptionKey != encryptionKey
		|| _cdnEncryptionIV != encryptionIV);
	_cdnDcId = dcId;
	_cdnToken = token;
	_cdnEncryptionKey = encryptionKey;
	_cdnEncryptionIV = encryptionIV;
	addCdnHashes(hashes);

	if (resendAllRequests && !_sentRequests.empty()) {
		auto resendRequests = std::vector<RequestData>();
		resendRequests.reserve(_sentRequests.size());
		while (!_sentRequests.empty()) {
			auto requestId = _sentRequests.begin()->first;
			MTP::cancel(requestId);
			resendRequests.push_back(finishSentRequest(requestId));
		}
		for (const auto &requestData : resendRequests) {
			makeRequest(requestData);
		}
	}
	makeRequest(requestData);
}

Storage::Cache::Key mtpFileLoader::cacheKey() const {
	return _location.match([&](const WebFileLocation &location) {
		return Data::WebDocumentCacheKey(location);
	}, [&](const GeoPointLocation &location) {
		return Data::GeoPointCacheKey(location);
	}, [&](const StorageFileLocation &location) {
		return location.cacheKey();
	});
}

std::optional<MediaKey> mtpFileLoader::fileLocationKey() const {
	if (_locationType != UnknownFileLocation) {
		return mediaKey(_locationType, dcId(), objId());
	}
	return std::nullopt;
}

webFileLoader::webFileLoader(
	const QString &url,
	const QString &to,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	QString(),
	0,
	0,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, _url(url) {
}

webFileLoader::~webFileLoader() {
	markAsNotSent();
}

bool webFileLoader::readyToRequest() const {
	return !_finished
		&& !_requestSent
		&& (_webLoadManager != FinishedWebLoadManager);
}

void webFileLoader::loadPart(int dcIndex) {
	Expects(readyToRequest());

	if (!_webLoadManager) {
		_webLoadMainManager = new WebLoadMainManager();

		_webLoadThread = new QThread();
		_webLoadManager = new WebLoadManager(_webLoadThread);

		_webLoadThread->start();
	}

	markAsSent();
	_webLoadManager->append(this, _url);
}

int webFileLoader::currentOffset() const {
	return _already;
}

void webFileLoader::loadProgress(qint64 already, qint64 size) {
	_size = size;
	_already = already;
	notifyAboutProgress();
}

void webFileLoader::loadFinished(const QByteArray &data) {
	markAsNotSent();
	if (writeResultPart(0, bytes::make_span(data))) {
		if (finalizeResult()) {
			notifyAboutProgress();
		}
	}
}

void webFileLoader::loadError() {
	markAsNotSent();
	cancel(true);
}

Storage::Cache::Key webFileLoader::cacheKey() const {
	return Data::UrlCacheKey(_url);
}

std::optional<MediaKey> webFileLoader::fileLocationKey() const {
	return std::nullopt;
}

void webFileLoader::cancelRequests() {
	if (!webLoadManager()) {
		return;
	}
	webLoadManager()->stop(this);
	markAsNotSent();
}

void webFileLoader::markAsSent() {
	if (_requestSent) {
		return;
	}
	_requestSent = true;
	_downloader->requestedAmountIncrement(0, 0, 1);
}

void webFileLoader::markAsNotSent() {
	if (!_requestSent) {
		return;
	}
	_requestSent = false;
	_downloader->requestedAmountIncrement(0, 0, -1);
}

class webFileLoaderPrivate {
public:
	webFileLoaderPrivate(webFileLoader *loader, const QString &url)
		: _interface(loader)
		, _url(url)
		, _redirectsLeft(kMaxHttpRedirects) {
	}

	QNetworkReply *reply() {
		return _reply;
	}

	QNetworkReply *request(QNetworkAccessManager &manager, const QString &redirect) {
		if (!redirect.isEmpty()) _url = redirect;

		QNetworkRequest req(_url);
		QByteArray rangeHeaderValue = "bytes=" + QByteArray::number(_already) + "-";
		req.setRawHeader("Range", rangeHeaderValue);
		_reply = manager.get(req);
		return _reply;
	}

	bool oneMoreRedirect() {
		if (_redirectsLeft) {
			--_redirectsLeft;
			return true;
		}
		return false;
	}

	void setData(const QByteArray &data) {
		_data = data;
	}
	void addData(const QByteArray &data) {
		_data.append(data);
	}
	const QByteArray &data() {
		return _data;
	}
	void setProgress(qint64 already, qint64 size) {
		_already = already;
		_size = qMax(size, 0LL);
	}

	qint64 size() const {
		return _size;
	}
	qint64 already() const {
		return _already;
	}

private:
	static constexpr auto kMaxHttpRedirects = 5;

	webFileLoader *_interface = nullptr;
	QUrl _url;
	qint64 _already = 0;
	qint64 _size = 0;
	QNetworkReply *_reply = nullptr;
	int32 _redirectsLeft = kMaxHttpRedirects;
	QByteArray _data;

	friend class WebLoadManager;
};

void stopWebLoadManager() {
	if (webLoadManager()) {
		_webLoadThread->quit();
		DEBUG_LOG(("Waiting for webloadThread to finish"));
		_webLoadThread->wait();
		delete _webLoadManager;
		delete _webLoadMainManager;
		delete _webLoadThread;
		_webLoadThread = nullptr;
		_webLoadMainManager = nullptr;
		_webLoadManager = FinishedWebLoadManager;
	}
}

WebLoadManager::WebLoadManager(QThread *thread) {
	moveToThread(thread);
	_manager.moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(process()));
	connect(thread, SIGNAL(finished()), this, SLOT(finish()));
	connect(this, SIGNAL(processDelayed()), this, SLOT(process()), Qt::QueuedConnection);

	connect(this, SIGNAL(progress(webFileLoader*,qint64,qint64)), _webLoadMainManager, SLOT(progress(webFileLoader*,qint64,qint64)));
	connect(this, SIGNAL(finished(webFileLoader*,QByteArray)), _webLoadMainManager, SLOT(finished(webFileLoader*,QByteArray)));
	connect(this, SIGNAL(error(webFileLoader*)), _webLoadMainManager, SLOT(error(webFileLoader*)));

	connect(&_manager, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)), this, SLOT(onFailed(QNetworkReply*)));
#ifndef OS_MAC_OLD
	connect(&_manager, SIGNAL(sslErrors(QNetworkReply*,const QList<QSslError>&)), this, SLOT(onFailed(QNetworkReply*)));
#endif // OS_MAC_OLD
}

WebLoadManager::~WebLoadManager() {
	clear();
}

void WebLoadManager::append(webFileLoader *loader, const QString &url) {
	loader->_private = new webFileLoaderPrivate(loader, url);

	QMutexLocker lock(&_loaderPointersMutex);
	_loaderPointers.insert(loader, loader->_private);
	emit processDelayed();
}

void WebLoadManager::stop(webFileLoader *loader) {
	QMutexLocker lock(&_loaderPointersMutex);
	_loaderPointers.remove(loader);
	emit processDelayed();
}

bool WebLoadManager::carries(webFileLoader *loader) const {
	QMutexLocker lock(&_loaderPointersMutex);
	return _loaderPointers.contains(loader);
}

bool WebLoadManager::handleReplyResult(webFileLoaderPrivate *loader, WebReplyProcessResult result) {
	QMutexLocker lock(&_loaderPointersMutex);
	LoaderPointers::iterator it = _loaderPointers.find(loader->_interface);
	if (it != _loaderPointers.cend() && it.key()->_private != loader) {
		it = _loaderPointers.end(); // it is a new loader which was realloced in the same address
	}
	if (it == _loaderPointers.cend()) {
		return false;
	}

	if (result == WebReplyProcessProgress) {
		if (loader->size() > Storage::kMaxFileInMemory) {
			LOG(("API Error: too large file is loaded to cache: %1").arg(loader->size()));
			result = WebReplyProcessError;
		}
	}
	if (result == WebReplyProcessError) {
		if (it != _loaderPointers.cend()) {
			emit error(it.key());
		}
		return false;
	}
	if (loader->already() < loader->size() || !loader->size()) {
		emit progress(it.key(), loader->already(), loader->size());
		return true;
	}
	emit finished(it.key(), loader->data());
	return false;
}

void WebLoadManager::onFailed(QNetworkReply::NetworkError error) {
	onFailed(qobject_cast<QNetworkReply*>(QObject::sender()));
}

void WebLoadManager::onFailed(QNetworkReply *reply) {
	if (!reply) return;
	reply->deleteLater();

	Replies::iterator j = _replies.find(reply);
	if (j == _replies.cend()) { // handled already
		return;
	}
	webFileLoaderPrivate *loader = j.value();
	_replies.erase(j);

	LOG(("Network Error: Failed to request '%1', error %2 (%3)").arg(QString::fromLatin1(loader->_url.toEncoded())).arg(int(reply->error())).arg(reply->errorString()));

	if (!handleReplyResult(loader, WebReplyProcessError)) {
		_loaders.remove(loader);
		delete loader;
	}
}

void WebLoadManager::onProgress(qint64 already, qint64 size) {
	const auto reply = qobject_cast<QNetworkReply*>(QObject::sender());
	if (!reply) return;

	const auto j = _replies.find(reply);
	if (j == _replies.cend()) { // handled already
		return;
	}
	const auto loader = j.value();

	auto result = WebReplyProcessProgress;
	const auto statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	const auto status = statusCode.isValid() ? statusCode.toInt() : 200;
	if (status != 200 && status != 206 && status != 416) {
		if (status == 301 || status == 302) {
			QString loc = reply->header(QNetworkRequest::LocationHeader).toString();
			if (!loc.isEmpty()) {
				if (loader->oneMoreRedirect()) {
					sendRequest(loader, loc);
					return;
				} else {
					LOG(("Network Error: Too many HTTP redirects in onFinished() for web file loader: %1").arg(loc));
					result = WebReplyProcessError;
				}
			}
		} else {
			LOG(("Network Error: Bad HTTP status received in WebLoadManager::onProgress(): %1").arg(statusCode.toInt()));
			result = WebReplyProcessError;
		}
	} else {
		loader->setProgress(already, size);
		QByteArray r = reply->readAll();
		if (!r.isEmpty()) {
			loader->addData(r);
		}
		if (size == 0) {
			LOG(("Network Error: Zero size received for HTTP download progress in WebLoadManager::onProgress(): %1 / %2").arg(already).arg(size));
			result = WebReplyProcessError;
		}
	}
	if (!handleReplyResult(loader, result)) {
		_replies.erase(j);
		_loaders.remove(loader);
		delete loader;

		reply->abort();
		reply->deleteLater();
	}
}

void WebLoadManager::onMeta() {
	const auto reply = qobject_cast<QNetworkReply*>(QObject::sender());
	if (!reply) return;

	const auto j = _replies.find(reply);
	if (j == _replies.cend()) { // handled already
		return;
	}
	const auto loader = j.value();

	const auto pairs = reply->rawHeaderPairs();
	for (const auto &pair : pairs) {
		if (QString::fromUtf8(pair.first).toLower() == "content-range") {
			const auto m = QRegularExpression(qsl("/(\\d+)([^\\d]|$)")).match(QString::fromUtf8(pair.second));
			if (m.hasMatch()) {
				loader->setProgress(qMax(qint64(loader->data().size()), loader->already()), m.captured(1).toLongLong());
				if (!handleReplyResult(loader, WebReplyProcessProgress)) {
					_replies.erase(j);
					_loaders.remove(loader);
					delete loader;

					reply->abort();
					reply->deleteLater();
				}
			}
		}
	}
}

void WebLoadManager::process() {
	Loaders newLoaders;
	{
		QMutexLocker lock(&_loaderPointersMutex);
		for (LoaderPointers::iterator i = _loaderPointers.begin(), e = _loaderPointers.end(); i != e; ++i) {
			Loaders::iterator it = _loaders.find(i.value());
			if (i.value()) {
				if (it == _loaders.cend()) {
					_loaders.insert(i.value());
					newLoaders.insert(i.value());
				}
				i.value() = 0;
			}
		}
		for (auto i = _loaders.begin(), e = _loaders.end(); i != e;) {
			LoaderPointers::iterator it = _loaderPointers.find((*i)->_interface);
			if (it != _loaderPointers.cend() && it.key()->_private != (*i)) {
				it = _loaderPointers.end();
			}
			if (it == _loaderPointers.cend()) {
				if (QNetworkReply *reply = (*i)->reply()) {
					_replies.remove(reply);
					reply->abort();
					reply->deleteLater();
				}
				delete (*i);
				i = _loaders.erase(i);
			} else {
				++i;
			}
		}
	}
	for_const (webFileLoaderPrivate *loader, newLoaders) {
		if (_loaders.contains(loader)) {
			sendRequest(loader);
		}
	}
}

void WebLoadManager::sendRequest(webFileLoaderPrivate *loader, const QString &redirect) {
	Replies::iterator j = _replies.find(loader->reply());
	if (j != _replies.cend()) {
		QNetworkReply *r = j.key();
		_replies.erase(j);

		r->abort();
		r->deleteLater();
	}

	QNetworkReply *r = loader->request(_manager, redirect);

	// Those use QObject::sender, so don't just remove the receiver pointer!
	connect(r, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(onProgress(qint64, qint64)));
	connect(r, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onFailed(QNetworkReply::NetworkError)));
	connect(r, SIGNAL(metaDataChanged()), this, SLOT(onMeta()));

	_replies.insert(r, loader);
}

void WebLoadManager::finish() {
	clear();
}

void WebLoadManager::clear() {
	QMutexLocker lock(&_loaderPointersMutex);
	for (auto i = _loaderPointers.begin(), e = _loaderPointers.end(); i != e; ++i) {
		if (i.value()) {
			i.key()->_private = nullptr;
		}
	}
	_loaderPointers.clear();

	for (const auto loader : _loaders) {
		delete loader;
	}
	_loaders.clear();

	for (auto i = _replies.begin(), e = _replies.end(); i != e; ++i) {
		delete i.key();
	}
	_replies.clear();
}

void WebLoadMainManager::progress(webFileLoader *loader, qint64 already, qint64 size) {
	if (webLoadManager() && webLoadManager()->carries(loader)) {
		loader->loadProgress(already, size);
	}
}

void WebLoadMainManager::finished(webFileLoader *loader, QByteArray data) {
	if (webLoadManager() && webLoadManager()->carries(loader)) {
		loader->loadFinished(data);
	}
}

void WebLoadMainManager::error(webFileLoader *loader) {
	if (webLoadManager() && webLoadManager()->carries(loader)) {
		loader->loadError();
	}
}
