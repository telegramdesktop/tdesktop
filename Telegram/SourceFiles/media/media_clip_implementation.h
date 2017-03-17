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

class FileLocation;

namespace Media {
namespace Clip {
namespace internal {

class ReaderImplementation {
public:
	ReaderImplementation(FileLocation *location, QByteArray *data)
		: _location(location)
		, _data(data) {
	}
	enum class Mode {
		Silent,
		Normal,
		Inspecting, // Not playing video, but reading data.
	};

	enum class ReadResult {
		Success,
		Error,
		EndOfFile,
	};
	// Read frames till current frame will have presentation time > frameMs, systemMs = getms().
	virtual ReadResult readFramesTill(TimeMs frameMs, TimeMs systemMs) = 0;

	// Get current frame real and presentation time.
	virtual TimeMs frameRealTime() const = 0;
	virtual TimeMs framePresentationTime() const = 0;

	// Render current frame to an image with specific size.
	virtual bool renderFrame(QImage &to, bool &hasAlpha, const QSize &size) = 0;

	virtual TimeMs durationMs() const = 0;
	virtual bool hasAudio() const = 0;
	virtual void pauseAudio() = 0;
	virtual void resumeAudio() = 0;

	virtual bool start(Mode mode, TimeMs &positionMs) = 0;

	virtual ~ReaderImplementation() {
	}
	int64 dataSize() const {
		return _dataSize;
	}

protected:
	FileLocation *_location;
	QByteArray *_data;
	QFile _file;
	QBuffer _buffer;
	QIODevice *_device = nullptr;
	int64 _dataSize = 0;

	void initDevice();

};

} // namespace internal
} // namespace Clip
} // namespace Media
