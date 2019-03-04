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

	[[nodiscard]] auto baseCacheKey() const
		->std::optional<Storage::Cache::Key> override;
	[[nodiscard]] int size() const override;

	void load(int offset) override;
	void cancel(int offset) override;
	void increasePriority() override;
	void stop() override;

	// Parts will be sent from the main thread.
	[[nodiscard]] rpl::producer<LoadedPart> parts() const override;

private:
	void fail();

	std::unique_ptr<QIODevice> _device;
	rpl::event_stream<LoadedPart> _parts;

};

std::unique_ptr<LoaderLocal> MakeFileLoader(const QString &path);
std::unique_ptr<LoaderLocal> MakeBytesLoader(const QByteArray &bytes);

} // namespace Streaming
} // namespace Media
