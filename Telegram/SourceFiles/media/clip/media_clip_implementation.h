/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QBuffer>

namespace Core {
class FileLocation;
} // namespace Core

namespace Media {
namespace Clip {
namespace internal {

class ReaderImplementation {
public:
	ReaderImplementation(Core::FileLocation *location, QByteArray *data)
	: _location(location)
	, _data(data) {
	}
	enum class Mode {
		Silent,
		Inspecting, // Not playing video, but reading data.
	};

	enum class ReadResult {
		Success,
		Error,
		EndOfFile,
	};
	// Read frames till current frame will have presentation time > frameMs, systemMs = crl::now().
	virtual ReadResult readFramesTill(crl::time frameMs, crl::time systemMs) = 0;

	// Get current frame real and presentation time.
	virtual crl::time frameRealTime() const = 0;
	virtual crl::time framePresentationTime() const = 0;

	// Render current frame to an image with specific size.
	virtual bool renderFrame(QImage &to, bool &hasAlpha, const QSize &size) = 0;

	virtual crl::time durationMs() const = 0;

	virtual bool start(Mode mode, crl::time &positionMs) = 0;

	virtual ~ReaderImplementation() {
	}
	int64 dataSize() const {
		return _dataSize;
	}

protected:
	Core::FileLocation *_location = nullptr;
	QByteArray *_data = nullptr;
	QFile _file;
	QBuffer _buffer;
	QIODevice *_device = nullptr;
	int64 _dataSize = 0;

	void initDevice();

};

} // namespace internal
} // namespace Clip
} // namespace Media
