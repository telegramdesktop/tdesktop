/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

extern "C" {

#include <libswscale/swscale.h>

} // extern "C"

#include "media/media_clip_implementation.h"
#include "media/media_child_ffmpeg_loader.h"

namespace Media {
namespace Clip {
namespace internal {

class FFMpegReaderImplementation : public ReaderImplementation {
public:
	FFMpegReaderImplementation(FileLocation *location, QByteArray *data, const AudioMsgId &audio);

	ReadResult readFramesTill(TimeMs frameMs, TimeMs systemMs) override;

	TimeMs frameRealTime() const override;
	TimeMs framePresentationTime() const override;

	bool renderFrame(QImage &to, bool &hasAlpha, const QSize &size) override;

	TimeMs durationMs() const override;
	bool hasAudio() const override {
		return (_audioStreamId >= 0);
	}

	bool start(Mode mode, TimeMs &positionMs) override;
	bool inspectAt(TimeMs &positionMs);

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
	PacketResult readPacket(AVPacket *packet);
	void processPacket(AVPacket *packet);
	TimeMs countPacketMs(AVPacket *packet) const;
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

	void startPacket();
	void finishPacket();
	void clearPacketQueue();

	static int _read(void *opaque, uint8_t *buf, int buf_size);
	static int64_t _seek(void *opaque, int64_t offset, int whence);

	Mode _mode = Mode::Normal;

	Rotation _rotation = Rotation::None;

	uchar *_ioBuffer = nullptr;
	AVIOContext *_ioContext = nullptr;
	AVFormatContext *_fmtContext = nullptr;
	AVCodec *_codec = nullptr;
	AVCodecContext *_codecContext = nullptr;
	int _streamId = 0;
	AVFrame *_frame = nullptr;
	bool _opened = false;
	bool _hadFrame = false;
	bool _frameRead = false;
	int _skippedInvalidDataPackets = 0;

	bool _hasAudioStream = false;
	int _audioStreamId = -1;
	AudioMsgId _audioMsgId;
	TimeMs _lastReadVideoMs = 0;
	TimeMs _lastReadAudioMs = 0;

	QQueue<FFMpeg::AVPacketDataWrap> _packetQueue;
	AVPacket _packetNull; // for final decoding
	int _packetStartedSize = 0;
	uint8_t *_packetStartedData = nullptr;
	bool _packetStarted = false;

	int _width = 0;
	int _height = 0;
	SwsContext *_swsContext = nullptr;
	QSize _swsSize;

	TimeMs _frameMs = 0;
	int _nextFrameDelay = 0;
	int _currentFrameDelay = 0;

	TimeMs _frameTime = 0;
	TimeMs _frameTimeCorrection = 0;

};

} // namespace internal
} // namespace Clip
} // namespace Media
