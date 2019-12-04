/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/file_download_mtproto.h"

#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "storage/cache/storage_cache_types.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_auth_key.h"
#include "base/openssl_help.h"
#include "facades.h"

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
	size,
	type,
	toCache,
	fromCloud,
	autoLoading,
	cacheTag)
, _downloader(&session().downloader())
, _dcId(location.dcId())
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
	size,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, _downloader(&session().downloader())
, _dcId(Global::WebFileDcId())
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
	size,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, _downloader(&session().downloader())
, _dcId(Global::WebFileDcId())
, _location(location) {
}

mtpFileLoader::~mtpFileLoader() {
	cancelRequests();
	_downloader->remove(this);
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

MTP::DcId mtpFileLoader::dcId() const {
	return _dcId;
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
	_nextRequestOffset += Storage::kDownloadPartSize;
}

mtpRequestId mtpFileLoader::sendRequest(const RequestData &requestData) {
	const auto offset = requestData.offset;
	const auto limit = Storage::kDownloadPartSize;
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
		Storage::kDownloadPartSize);
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
		-Storage::kDownloadPartSize);
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
	if (finished) {
		_downloader->remove(this);
		if (!finalizeResult()) {
			return false;
		}
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

void mtpFileLoader::startLoading() {
	_downloader->enqueue(this);
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
