/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_common.h"
#include "media/streaming/media_streaming_utility.h"
#include "media/streaming/media_streaming_reader.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "base/bytes.h"
#include "base/weak_ptr.h"

#include <thread>

namespace Media {
namespace Streaming {

class FileDelegate;

class File final {
public:
	explicit File(std::shared_ptr<Reader> reader);

	File(const File &other) = delete;
	File &operator=(const File &other) = delete;

	void start(not_null<FileDelegate*> delegate, crl::time position);
	void wake();
	void stop(bool stillActive = false);

	[[nodiscard]] bool isRemoteLoader() const;
	void setLoaderPriority(int priority);

	~File();

private:
	class Context final : public base::has_weak_ptr {
	public:
		Context(not_null<FileDelegate*> delegate, not_null<Reader*> reader);
		~Context();

		void start(crl::time position);
		void readNextPacket();

		void interrupt();
		void wake();
		[[nodiscard]] bool interrupted() const;
		[[nodiscard]] bool failed() const;
		[[nodiscard]] bool finished() const;

		void stopStreamingAsync();

	private:
		enum class SleepPolicy {
			Allowed,
			Disallowed,
		};
		static int Read(void *opaque, uint8_t *buffer, int bufferSize);
		static int64_t Seek(void *opaque, int64_t offset, int whence);

		[[nodiscard]] int read(bytes::span buffer);
		[[nodiscard]] int64_t seek(int64_t offset, int whence);

		[[nodiscard]] bool unroll() const;
		void logError(QLatin1String method);
		void logError(QLatin1String method, FFmpeg::AvErrorWrap error);
		void logFatal(QLatin1String method);
		void logFatal(QLatin1String method, FFmpeg::AvErrorWrap error);
		void fail(Error error);

		Stream initStream(
			not_null<AVFormatContext *> format,
			AVMediaType type);
		void seekToPosition(
			not_null<AVFormatContext *> format,
			const Stream &stream,
			crl::time position);

		// TODO base::expected.
		[[nodiscard]] auto readPacket()
		-> std::variant<FFmpeg::Packet, FFmpeg::AvErrorWrap>;
		void processQueuedPackets(SleepPolicy policy);

		void handleEndOfFile();
		void sendFullInCache(bool force = false);

		const not_null<FileDelegate*> _delegate;
		const not_null<Reader*> _reader;

		base::flat_map<int, std::vector<FFmpeg::Packet>> _queuedPackets;
		int _offset = 0;
		int _size = 0;
		bool _failed = false;
		bool _readTillEnd = false;
		std::optional<bool> _fullInCache;
		crl::semaphore _semaphore;
		std::atomic<bool> _interrupted = false;

		FFmpeg::FormatPointer _format;

	};

	std::optional<Context> _context;
	std::shared_ptr<Reader> _reader;
	std::thread _thread;

};

} // namespace Streaming
} // namespace Media
