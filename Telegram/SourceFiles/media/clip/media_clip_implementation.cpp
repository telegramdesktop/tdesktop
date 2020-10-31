/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/clip/media_clip_implementation.h"

#include "core/file_location.h"

namespace Media {
namespace Clip {
namespace internal {

void ReaderImplementation::initDevice() {
	if (_data->isEmpty()) {
		if (_file.isOpen()) _file.close();
		_file.setFileName(_location->name());
		_dataSize = _file.size();
	} else {
		if (_buffer.isOpen()) _buffer.close();
		_buffer.setBuffer(_data);
		_dataSize = _data->size();
	}
	_device = _data->isEmpty() ? static_cast<QIODevice*>(&_file) : static_cast<QIODevice*>(&_buffer);
}

} // namespace internal
} // namespace Clip
} // namespace Media
