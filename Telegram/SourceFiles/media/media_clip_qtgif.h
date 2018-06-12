/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/media_clip_implementation.h"

namespace Media {
namespace Clip {
namespace internal {

class QtGifReaderImplementation : public ReaderImplementation {
public:

	QtGifReaderImplementation(FileLocation *location, QByteArray *data);

	ReadResult readFramesTill(TimeMs frameMs, TimeMs systemMs) override;

	TimeMs frameRealTime() const override;
	TimeMs framePresentationTime() const override;

	bool renderFrame(QImage &to, bool &hasAlpha, const QSize &size) override;

	TimeMs durationMs() const override;
	bool hasAudio() const override {
		return false;
	}

	bool start(Mode mode, TimeMs &positionMs) override;

	~QtGifReaderImplementation();

private:
	bool jumpToStart();
	ReadResult readNextFrame();

	Mode _mode = Mode::Normal;

	std::unique_ptr<QImageReader> _reader;
	int _framesLeft = 0;
	TimeMs _frameRealTime = 0;
	TimeMs _frameTime = 0;
	int _frameDelay = 0;
	QImage _frame;

};

} // namespace internal
} // namespace Clip
} // namespace Media
