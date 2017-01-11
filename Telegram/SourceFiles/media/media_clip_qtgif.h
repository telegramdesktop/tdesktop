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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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
	void pauseAudio() override {
	}
	void resumeAudio() override {
	}

	bool start(Mode mode, TimeMs &positionMs) override;

	~QtGifReaderImplementation();

private:
	bool jumpToStart();
	ReadResult readNextFrame();

	Mode _mode = Mode::Normal;

	QImageReader *_reader = nullptr;
	int _framesLeft = 0;
	TimeMs _frameRealTime = 0;
	TimeMs _frameTime = 0;
	int _frameDelay = 0;
	QImage _frame;

};

} // namespace internal
} // namespace Clip
} // namespace Media
