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

class LoaderLocal : public Loader, public base::has_weak_ptr {
public:
	LoaderLocal(std::unique_ptr<QIODevice> device);

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

	void attachDownloader(
		not_null<Storage::StreamedFileDownloader*> downloader) override;
	void clearAttachedDownloader() override;

private:
	void fail();

	const std::unique_ptr<QIODevice> _device;
	const int64 _size = 0;
	rpl::event_stream<LoadedPart> _parts;

};

std::unique_ptr<LoaderLocal> MakeFileLoader(const QString &path);
std::unique_ptr<LoaderLocal> MakeBytesLoader(const QByteArray &bytes);

} // namespace Streaming
} // namespace Media
