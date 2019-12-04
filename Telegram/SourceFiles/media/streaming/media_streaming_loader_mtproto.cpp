/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_loader_mtproto.h"

#include "apiwrap.h"
#include "main/main_session.h"
#include "storage/streamed_file_downloader.h"
#include "storage/cache/storage_cache_types.h"

namespace Media {
namespace Streaming {
namespace {

constexpr auto kMaxConcurrentRequests = 4;

} // namespace

LoaderMtproto::LoaderMtproto(
	not_null<Storage::DownloadManager*> owner,
	const StorageFileLocation &location,
	int size,
	Data::FileOrigin origin)
: _owner(owner)
, _location(location)
, _dcId(location.dcId())
, _size(size)
, _origin(origin)
, _api(_owner->api().instance()) {
}

LoaderMtproto::~LoaderMtproto() {
	for (const auto [index, amount] : _amountByDcIndex) {
		changeRequestedAmount(index, -amount);
	}
	_owner->remove(this);
}

std::optional<Storage::Cache::Key> LoaderMtproto::baseCacheKey() const {
	return _location.bigFileBaseCacheKey();
}

int LoaderMtproto::size() const {
	return _size;
}

void LoaderMtproto::load(int offset) {
	crl::on_main(this, [=] {
		if (_downloader) {
			auto bytes = _downloader->readLoadedPart(offset);
			if (!bytes.isEmpty()) {
				cancelForOffset(offset);
				_parts.fire({ offset, std::move(bytes) });
				return;
			}
		}
		if (_requests.contains(offset)) {
			return;
		} else if (_requested.add(offset)) {
			_owner->enqueue(this); // #TODO download priority
		}
	});
}

void LoaderMtproto::stop() {
	crl::on_main(this, [=] {
		ranges::for_each(
			base::take(_requests),
			_api.requestCanceller(),
			&base::flat_map<int, mtpRequestId>::value_type::second);
		_requested.clear();
		_owner->remove(this);
	});
}

void LoaderMtproto::cancel(int offset) {
	crl::on_main(this, [=] {
		cancelForOffset(offset);
	});
}

void LoaderMtproto::cancelForOffset(int offset) {
	if (const auto requestId = _requests.take(offset)) {
		_api.request(*requestId).cancel();
		_owner->enqueue(this);
	} else {
		_requested.remove(offset);
	}
}

void LoaderMtproto::attachDownloader(
		not_null<Storage::StreamedFileDownloader*> downloader) {
	_downloader = downloader;
}

void LoaderMtproto::clearAttachedDownloader() {
	_downloader = nullptr;
}

void LoaderMtproto::increasePriority() {
	crl::on_main(this, [=] {
		_requested.increasePriority();
	});
}

void LoaderMtproto::changeRequestedAmount(int index, int amount) {
	_owner->requestedAmountIncrement(_dcId, index, amount);
	_amountByDcIndex[index] += amount;
}

MTP::DcId LoaderMtproto::dcId() const {
	return _dcId;
}

bool LoaderMtproto::readyToRequest() const {
	return !_requested.empty();
}

void LoaderMtproto::loadPart(int dcIndex) {
	const auto offset = _requested.take().value_or(-1);
	if (offset < 0) {
		return;
	}

	changeRequestedAmount(dcIndex, kPartSize);

	const auto usedFileReference = _location.fileReference();
	const auto id = _api.request(MTPupload_GetFile(
		MTP_flags(0),
		_location.tl(Auth().userId()),
		MTP_int(offset),
		MTP_int(kPartSize)
	)).done([=](const MTPupload_File &result) {
		changeRequestedAmount(dcIndex, -kPartSize);
		requestDone(offset, result);
	}).fail([=](const RPCError &error) {
		changeRequestedAmount(dcIndex, -kPartSize);
		requestFailed(offset, error, usedFileReference);
	}).toDC(
		MTP::downloadDcId(_dcId, dcIndex)
	).send();
	_requests.emplace(offset, id);
}

void LoaderMtproto::requestDone(int offset, const MTPupload_File &result) {
	result.match([&](const MTPDupload_file &data) {
		_requests.erase(offset);
		_owner->enqueue(this);
		_parts.fire({ offset, data.vbytes().v });
	}, [&](const MTPDupload_fileCdnRedirect &data) {
		changeCdnParams(
			offset,
			data.vdc_id().v,
			data.vfile_token().v,
			data.vencryption_key().v,
			data.vencryption_iv().v,
			data.vfile_hashes().v);
	});
}

void LoaderMtproto::changeCdnParams(
		int offset,
		MTP::DcId dcId,
		const QByteArray &token,
		const QByteArray &encryptionKey,
		const QByteArray &encryptionIV,
		const QVector<MTPFileHash> &hashes) {
	// #TODO streaming later cdn
	_parts.fire({ LoadedPart::kFailedOffset });
}

void LoaderMtproto::requestFailed(
		int offset,
		const RPCError &error,
		const QByteArray &usedFileReference) {
	const auto &type = error.type();
	const auto fail = [=] {
		_parts.fire({ LoadedPart::kFailedOffset });
	};
	if (error.code() != 400 || !type.startsWith(qstr("FILE_REFERENCE_"))) {
		return fail();
	}
	const auto callback = [=](const Data::UpdatedFileReferences &updated) {
		_location.refreshFileReference(updated);
		if (_location.fileReference() == usedFileReference) {
			fail();
		} else if (!_requests.take(offset)) {
			// Request with such offset was already cancelled.
			return;
		} else {
			_requested.add(offset);
			_owner->enqueue(this);
		}
	};
	_owner->api().refreshFileReference(
		_origin,
		crl::guard(this, callback));
}

rpl::producer<LoadedPart> LoaderMtproto::parts() const {
	return _parts.events();
}

} // namespace Streaming
} // namespace Media
