/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/clip/media_clip_implementation.h"
#include "ffmpeg/ffmpeg_utility.h"

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
} // extern "C"
#include <deque>

//#include "media/streaming/media_streaming_utility.h"

namespace Media {
namespace Clip {
namespace internal {

constexpr auto kMaxInMemory = 10 * 1024 * 1024;

class FFMpegReaderImplementation : public ReaderImplementation {
public:
	FFMpegReaderImplementation(Core::FileLocation *location, QByteArray *data);

	ReadResult readFramesTill(crl::time frameMs, crl::time systemMs) override;

	crl::time frameRealTime() const override;
	crl::time framePresentationTime() const override;

	bool renderFrame(QImage &to, bool &hasAlpha, const QSize &size) override;

	crl::time durationMs() const override;

	bool start(Mode mode, crl::time &positionMs) override;
	bool inspectAt(crl::time &positionMs);

	QString logData() const;

	bool isGifv() const;

	~FFMpegReaderImplementation();

private:
	ReadResult readNextFrame();
	void processReadFrame();

	enum class PacketResult {
		Ok,
		EndOfFile,
		Error,
	};
	PacketResult readPacket(FFmpeg::Packet &packet);
	void processPacket(FFmpeg::Packet &&packet);
	crl::time countPacketMs(const FFmpeg::Packet &packet) const;
	PacketResult readAndProcessPacket();

	enum class Rotation {
		None,
		Degrees90,
		Degrees180,
		Degrees270,
	};
	Rotation rotationFromDegrees(int degrees) const;
	bool rotationSwapWidthHeight() const {
		return (_rotation == Rotation::Degrees90) || (_rotation == Rotation::Degrees270);
	}

	static int _read(void *opaque, uint8_t *buf, int buf_size);
	static int64_t _seek(void *opaque, int64_t offset, int whence);

	Mode _mode = Mode::Silent;

	Rotation _rotation = Rotation::None;

	uchar *_ioBuffer = nullptr;
	AVIOContext *_ioContext = nullptr;
	AVFormatContext *_fmtContext = nullptr;
	AVCodecContext *_codecContext = nullptr;
	int _streamId = 0;
	FFmpeg::FramePointer _frame;
	bool _opened = false;
	bool _hadFrame = false;
	bool _frameRead = false;
	int _skippedInvalidDataPackets = 0;

	bool _hasAudioStream = false;
	crl::time _lastReadVideoMs = 0;
	crl::time _lastReadAudioMs = 0;

	std::deque<FFmpeg::Packet> _packetQueue;

	int _width = 0;
	int _height = 0;
	SwsContext *_swsContext = nullptr;
	QSize _swsSize;

	crl::time _frameMs = 0;
	int _nextFrameDelay = 0;
	int _currentFrameDelay = 0;

	crl::time _frameTime = 0;
	crl::time _frameTimeCorrection = 0;

};

} // namespace internal
} // namespace Clip
} // namespace Media
