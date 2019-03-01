/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_loader.h"
#include "mtproto/sender.h"
#include "data/data_file_origin.h"

class ApiWrap;

namespace Media {
namespace Streaming {

class LoaderMtproto : public Loader, public base::has_weak_ptr {
public:
	LoaderMtproto(
		not_null<ApiWrap*> api,
		MTP::DcId dcId,
		const MTPInputFileLocation &location,
		int size,
		Data::FileOrigin origin);

	[[nodiscard]] auto baseCacheKey() const
	-> std::optional<Storage::Cache::Key> override;
	[[nodiscard]] int size() const override;

	void load(int offset) override;
	void cancel(int offset) override;
	void increasePriority() override;
	void stop() override;

	// Parts will be sent from the main thread.
	[[nodiscard]] rpl::producer<LoadedPart> parts() const override;

	~LoaderMtproto();

private:
	void sendNext();

	void requestDone(int offset, const MTPupload_File &result);
	void requestFailed(
		int offset, 
		const RPCError &error, 
		const QByteArray &usedFileReference);
	void changeCdnParams(
		int offset,
		MTP::DcId dcId,
		const QByteArray &token,
		const QByteArray &encryptionKey,
		const QByteArray &encryptionIV,
		const QVector<MTPFileHash> &hashes);

	[[nodiscard]] QByteArray locationFileReference() const;

	const not_null<ApiWrap*> _api;
	const MTP::DcId _dcId = 0;

	// _location can be changed with an updated file_reference.
	MTPInputFileLocation _location;

	const int _size = 0;
	const Data::FileOrigin _origin;

	MTP::Sender _sender;

	PriorityQueue _requested;
	base::flat_map<int, mtpRequestId> _requests;
	rpl::event_stream<LoadedPart> _parts;

};

} // namespace Streaming
} // namespace Media
