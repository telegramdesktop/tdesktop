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
#include "base/bytes.h"
#include "base/weak_ptr.h"

#include <thread>

namespace Data {
class Session;
} // namespace Data

namespace Media {
namespace Streaming {

class Loader;
class FileDelegate;

class File final {
public:
	File(not_null<Data::Session*> owner, std::unique_ptr<Loader> loader);

	File(const File &other) = delete;
	File &operator=(const File &other) = delete;

	void start(not_null<FileDelegate*> delegate, crl::time position);
	void wake();
	void stop();

	[[nodiscard]] bool isRemoteLoader() const;

	~File();

private:
	class Context final : public base::has_weak_ptr {
	public:
		Context(not_null<FileDelegate*> delegate, not_null<Reader*> reader);

		void start(crl::time position);
		void readNextPacket();

		void interrupt();
		void wake();
		[[nodiscard]] bool interrupted() const;
		[[nodiscard]] bool failed() const;
		[[nodiscard]] bool finished() const;

		~Context();

	private:
		static int Read(void *opaque, uint8_t *buffer, int bufferSize);
		static int64_t Seek(void *opaque, int64_t offset, int whence);

		[[nodiscard]] int read(bytes::span buffer);
		[[nodiscard]] int64_t seek(int64_t offset, int whence);

		[[nodiscard]] bool unroll() const;
		void logError(QLatin1String method);
		void logError(QLatin1String method, AvErrorWrap error);
		void logFatal(QLatin1String method);
		void logFatal(QLatin1String method, AvErrorWrap error);
		void fail(Error error);

		Stream initStream(
			not_null<AVFormatContext *> format,
			AVMediaType type);
		void seekToPosition(
			not_null<AVFormatContext *> format,
			const Stream &stream,
			crl::time position);

		// TODO base::expected.
		[[nodiscard]] base::variant<Packet, AvErrorWrap> readPacket();

		void handleEndOfFile();

		const not_null<FileDelegate*> _delegate;
		const not_null<Reader*> _reader;

		int _offset = 0;
		int _size = 0;
		bool _failed = false;
		bool _readTillEnd = false;
		crl::semaphore _semaphore;
		std::atomic<bool> _interrupted = false;

		FormatPointer _format;

	};

	std::optional<Context> _context;
	Reader _reader;
	std::thread _thread;

};

} // namespace Streaming
} // namespace Media
