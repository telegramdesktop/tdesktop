/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_common.h"
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

class File final {
public:
	File(not_null<Data::Session*> owner, std::unique_ptr<Loader> loader);

	File(const File &other) = delete;
	File &operator=(const File &other) = delete;

	void start(Mode mode, crl::time positionTime);

	//rpl::producer<Information> information() const;
	//rpl::producer<Packet> video() const;
	//rpl::producer<Packet> audio() const;

	~File();

private:
	void finish();

	class Context final : public base::has_weak_ptr {
	public:
		Context(not_null<Reader*> reader);

		void start(Mode mode, crl::time positionTime);
		void readNextPacket();

		void interrupt();
		[[nodiscard]] bool interrupted() const;
		[[nodiscard]] bool failed() const;
		[[nodiscard]] bool started() const;
		[[nodiscard]] bool finished() const;
		[[nodiscard]] const Information &information() const;

		~Context();

	private:
		struct StreamWrap {
			int id = -1;
			AVStream *info = nullptr;
			Stream stream;
			QMutex mutex;
		};

		static int Read(void *opaque, uint8_t *buffer, int bufferSize);
		static int64_t Seek(void *opaque, int64_t offset, int whence);

		[[nodiscard]] int read(bytes::span buffer);
		[[nodiscard]] int64_t seek(int64_t offset, int whence);

		[[nodiscard]] bool unroll() const;
		void logError(QLatin1String method);
		void logError(QLatin1String method, AvErrorWrap error);
		void logFatal(QLatin1String method);
		void logFatal(QLatin1String method, AvErrorWrap error);

		void initStream(StreamWrap &wrap, AVMediaType type);
		void seekToPosition(crl::time positionTime);

		// #TODO base::expected.
		[[nodiscard]] base::variant<Packet, AvErrorWrap> readPacket();

		void processPacket(Packet &&packet);
		[[nodiscard]] const StreamWrap &mainStreamUnchecked() const;
		[[nodiscard]] const StreamWrap &mainStream() const;
		[[nodiscard]] StreamWrap &mainStream();
		void readInformation(crl::time positionTime);

		[[nodiscard]] QImage readFirstVideoFrame();
		[[nodiscard]] auto tryReadFirstVideoFrame()
			-> base::variant<QImage, AvErrorWrap>;

		void enqueueEofPackets();

		static void ClearStream(StreamWrap &wrap);
		[[nodiscard]] static crl::time CountPacketPositionTime(
			not_null<const AVStream*> info,
			const Packet &packet);
		[[nodiscard]] static crl::time CountPacketPositionTime(
			const StreamWrap &wrap,
			const Packet &packet);
		static void Enqueue(StreamWrap &wrap, Packet &&packet);

		not_null<Reader*> _reader;
		Mode _mode = Mode::Both;
		int _offset = 0;
		int _size = 0;
		bool _failed = false;
		bool _opened = false;
		bool _readTillEnd = false;
		crl::semaphore _semaphore;
		std::atomic<bool> _interrupted = false;
		std::optional<Information> _information;

		uchar *_ioBuffer = nullptr;
		AVIOContext *_ioContext = nullptr;
		AVFormatContext *_formatContext = nullptr;

		StreamWrap _video;
		StreamWrap _audio;

		AudioMsgId _audioMsgId;

	};

	std::thread _thread;
	Reader _reader;
	std::unique_ptr<Context> _context;

};

} // namespace Streaming
} // namespace Media
