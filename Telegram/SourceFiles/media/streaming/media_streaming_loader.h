/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media {
namespace Streaming {

struct LoadedPart {
	int offset = 0;
	QByteArray bytes;

	static constexpr auto kFailedOffset = -1;
};

class Loader {
public:
	static constexpr auto kPartSize = 128 * 1024;

	//[[nodiscard]] virtual Storage::Cache::Key baseCacheKey() const = 0;
	[[nodiscard]] virtual int size() const = 0;

	virtual void load(int offset, int till = -1) = 0;
	virtual void stop() = 0;

	// Parts will be sent from the main thread.
	[[nodiscard]] virtual rpl::producer<LoadedPart> parts() const = 0;

	virtual ~Loader() = default;

};

} // namespace Streaming
} // namespace Media
