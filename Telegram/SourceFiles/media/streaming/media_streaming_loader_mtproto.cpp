/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_loader_mtproto.h"

#include "apiwrap.h"

namespace Media {
namespace Streaming {
namespace {

constexpr auto kMaxConcurrentRequests = 1; // #TODO streaming

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

int LoaderMtproto::size() const {
	return _size;
}

void LoaderMtproto::load(int offset, int till) {
	crl::on_main(this, [=] {
		cancelRequestsBefore(offset);
		_till = till;
		sendNext(offset);
	});
}

void LoaderMtproto::sendNext(int possibleOffset) {
	Expects((possibleOffset % kPartSize) == 0);

	const auto offset = _requests.empty()
		? possibleOffset
		: _requests.back().first + kPartSize;
	if ((_till >= 0 && offset >= _till) || (_size > 0 && offset >= _size)) {
		return;
	} else if (_requests.size() >= kMaxConcurrentRequests) {
		return;
	}

	static auto DcIndex = 0;
	const auto id = _sender.request(MTPupload_GetFile(
		_location,
		MTP_int(offset),
		MTP_int(kPartSize)
	)).done([=](const MTPupload_File &result) {
		requestDone(offset, result);
	}).fail([=](const RPCError &error) {
		requestFailed(offset, error);
	}).toDC(
		MTP::downloadDcId(_dcId, (++DcIndex) % MTP::kDownloadSessionsCount)
	).send();
	_requests.emplace(offset, id);

	sendNext(offset + kPartSize);
}

void LoaderMtproto::requestDone(int offset, const MTPupload_File &result) {
	result.match([&](const MTPDupload_file &data) {
		_requests.erase(offset);
		if (data.vbytes.v.size() == kPartSize) {
			sendNext(offset + kPartSize);
		}
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
	// #TODO streaming
}

void LoaderMtproto::requestFailed(int offset, const RPCError &error) {
	const auto &type = error.type();
	if (error.code() != 400 || !type.startsWith(qstr("FILE_REFERENCE_"))) {
		_parts.fire({ LoadedPart::kFailedOffset });
		return;
	}
	const auto callback = [=](const Data::UpdatedFileReferences &updated) {
		// #TODO streaming
	};
	_api->refreshFileReference(_origin, crl::guard(this, callback));
}

void LoaderMtproto::stop() {
	crl::on_main(this, [=] {
		for (const auto [offset, requestId] : base::take(_requests)) {
			_sender.request(requestId).cancel();
		}
	});
}

rpl::producer<LoadedPart> LoaderMtproto::parts() const {
	return _parts.events();
}

LoaderMtproto::~LoaderMtproto() = default;

void LoaderMtproto::cancelRequestsBefore(int offset) {
	const auto from = begin(_requests);
	const auto till = ranges::lower_bound(
		_requests,
		offset,
		ranges::less(),
		[](auto pair) { return pair.first; });
	ranges::for_each(
		from,
		till,
		_sender.requestCanceller(),
		&base::flat_map<int, mtpRequestId>::value_type::second);
	_requests.erase(from, till);
}

} // namespace Streaming
} // namespace Media
