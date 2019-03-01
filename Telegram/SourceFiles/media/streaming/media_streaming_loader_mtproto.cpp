/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_loader_mtproto.h"

#include "apiwrap.h"
#include "storage/cache/storage_cache_types.h"

namespace Media {
namespace Streaming {
namespace {

constexpr auto kMaxConcurrentRequests = 2;
constexpr auto kDocumentBaseCacheTag = 0x0000000000010000ULL;
constexpr auto kDocumentBaseCacheMask = 0x000000000000FF00ULL;

} // namespace

LoaderMtproto::LoaderMtproto(
	not_null<ApiWrap*> api,
	MTP::DcId dcId,
	const MTPInputFileLocation &location,
	int size,
	Data::FileOrigin origin)
: _api(api)
, _dcId(dcId)
, _location(location)
, _size(size)
, _origin(origin) {
}

std::optional<Storage::Cache::Key> LoaderMtproto::baseCacheKey() const {
	return _location.match([&](const MTPDinputDocumentFileLocation &data) {
		const auto id = data.vid.v;
		const auto high = kDocumentBaseCacheTag
			| ((uint64(_dcId) << 16) & kDocumentBaseCacheMask)
			| (id >> 48);
		const auto low = (id << 16);

		Ensures((low & 0xFFULL) == 0);
		return Storage::Cache::Key{ high, low };
	}, [](auto &&) -> Storage::Cache::Key {
		Unexpected("Not implemented file location type.");
	});
}

int LoaderMtproto::size() const {
	return _size;
}

void LoaderMtproto::load(int offset) {
	crl::on_main(this, [=] {
		if (_requests.contains(offset)) {
			return;
		} else if (_requested.add(offset)) {
			sendNext();
		}
	});
}

void LoaderMtproto::stop() {
	crl::on_main(this, [=] {
		ranges::for_each(
			base::take(_requests),
			_sender.requestCanceller(),
			&base::flat_map<int, mtpRequestId>::value_type::second);
		_requested.clear();
	});
}

void LoaderMtproto::cancel(int offset) {
	crl::on_main(this, [=] {
		if (const auto requestId = _requests.take(offset)) {
			_sender.request(*requestId).cancel();
			sendNext();
		} else {
			_requested.remove(offset);
		}
	});
}

void LoaderMtproto::increasePriority() {
	crl::on_main(this, [=] {
		_requested.increasePriority();
	});
}

void LoaderMtproto::sendNext() {
	if (_requests.size() >= kMaxConcurrentRequests) {
		return;
	}
	const auto offset = _requested.take().value_or(-1);
	if (offset < 0) {
		return;
	}

	static auto DcIndex = 0;
	const auto reference = locationFileReference();
	const auto id = _sender.request(MTPupload_GetFile(
		_location,
		MTP_int(offset),
		MTP_int(kPartSize)
	)).done([=](const MTPupload_File &result) {
		requestDone(offset, result);
	}).fail([=](const RPCError &error) {
		requestFailed(offset, error, reference);
	}).toDC(
		MTP::downloadDcId(_dcId, (++DcIndex) % MTP::kDownloadSessionsCount)
	).send();
	_requests.emplace(offset, id);

	sendNext();
}

void LoaderMtproto::requestDone(int offset, const MTPupload_File &result) {
	result.match([&](const MTPDupload_file &data) {
		_requests.erase(offset);
		sendNext();
		_parts.fire({ offset, data.vbytes.v });
	}, [&](const MTPDupload_fileCdnRedirect &data) {
		changeCdnParams(
			offset,
			data.vdc_id.v,
			data.vfile_token.v,
			data.vencryption_key.v,
			data.vencryption_iv.v,
			data.vfile_hashes.v);
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
		_location.match([&](const MTPDinputDocumentFileLocation &location) {
			const auto i = updated.data.find(location.vid.v);
			if (i == end(updated.data)) {
				return fail();
			}
			const auto reference = i->second;
			if (reference == usedFileReference) {
				return fail();
			} else if (reference != location.vfile_reference.v) {
				_location = MTP_inputDocumentFileLocation(
					MTP_long(location.vid.v),
					MTP_long(location.vaccess_hash.v),
					MTP_bytes(reference));
			}
			if (!_requests.take(offset)) {
				// Request with such offset was already cancelled.
				return;
			}
			_requested.add(offset);
			sendNext();
		}, [](auto &&) {
			Unexpected("Not implemented file location type.");
		});
	};
	_api->refreshFileReference(_origin, crl::guard(this, callback));
}

QByteArray LoaderMtproto::locationFileReference() const {
	return _location.match([&](const MTPDinputDocumentFileLocation &data) {
		return data.vfile_reference.v;
	}, [](auto &&) -> QByteArray {
		Unexpected("Not implemented file location type.");
	});
}

rpl::producer<LoadedPart> LoaderMtproto::parts() const {
	return _parts.events();
}

LoaderMtproto::~LoaderMtproto() = default;

} // namespace Streaming
} // namespace Media
