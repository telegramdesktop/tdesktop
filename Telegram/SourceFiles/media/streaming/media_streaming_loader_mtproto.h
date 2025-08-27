/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "media/streaming/media_streaming_loader.h"
#include "mtproto/sender.h"
#include "data/data_file_origin.h"
#include "storage/download_manager_mtproto.h"

namespace Media {
namespace Streaming {

class LoaderMtproto : public Loader, public Storage::DownloadMtprotoTask {
public:
	LoaderMtproto(
		not_null<Storage::DownloadManagerMtproto*> owner,
		const StorageFileLocation &location,
		int64 size,
		Data::FileOrigin origin);

	[[nodiscard]] Storage::Cache::Key baseCacheKey() const override;
	[[nodiscard]] int64 size() const override;

	void load(int64 offset) override;
	void cancel(int64 offset) override;
	void resetPriorities() override;
	void setPriority(int priority) override;
	void stop() override;

	void tryRemoveFromQueue() override;

	// Parts will be sent from the main thread.
	[[nodiscard]] rpl::producer<LoadedPart> parts() const override;
	[[nodiscard]] rpl::producer<SpeedEstimate> speedEstimate() const override;

	void attachDownloader(
		not_null<Storage::StreamedFileDownloader*> downloader) override;
	void clearAttachedDownloader() override;

private:
	struct StatsEntry {
		crl::time start = 0;
		crl::time end = 0;
		int64 offset = 0;
	};

	bool readyToRequest() const override;
	int64 takeNextRequestOffset() override;
	bool feedPart(int64 offset, const QByteArray &bytes) override;
	void cancelOnFail() override;

	void cancelForOffset(int64 offset);
	void addToQueueWithPriority();

	void checkStats();

	const int64 _size = 0;
	int _priority = 0;

	MTP::Sender _api;

	PriorityQueue _requested;
	rpl::event_stream<LoadedPart> _parts;
	rpl::event_stream<SpeedEstimate> _speedEstimate;

	std::vector<StatsEntry> _stats;
	crl::time _firstRequestStart = 0;
	base::Timer _statsTimer;

	Storage::StreamedFileDownloader *_downloader = nullptr;

};

} // namespace Streaming
} // namespace Media
