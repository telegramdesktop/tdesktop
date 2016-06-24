/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include "media/media_clip_implementation.h"

namespace Media {
namespace Clip {
namespace internal {

class FFMpegReaderImplementation : public ReaderImplementation {
public:

	FFMpegReaderImplementation(FileLocation *location, QByteArray *data);

	bool readNextFrame() override;
	bool renderFrame(QImage &to, bool &hasAlpha, const QSize &size) override;
	int nextFrameDelay() override;
	bool start(bool onlyGifv) override;

	int duration() const;
	QString logData() const;

	~FFMpegReaderImplementation();

private:
	void rememberPacket();
	void freePacket();

	static int _read(void *opaque, uint8_t *buf, int buf_size);
	static int64_t _seek(void *opaque, int64_t offset, int whence);

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

	int _audioStreamId = 0;

	AVPacket _avpkt;
	int _packetSize = 0;
	uint8_t *_packetData = nullptr;
	bool _packetWas = false;

	int _width = 0;
	int _height = 0;
	SwsContext *_swsContext = nullptr;
	QSize _swsSize;

	int64 _frameMs = 0;
	int _nextFrameDelay = 0;
	int _currentFrameDelay = 0;

};

} // namespace internal
} // namespace Clip
} // namespace Media
