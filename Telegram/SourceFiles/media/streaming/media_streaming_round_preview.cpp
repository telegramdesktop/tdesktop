/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_round_preview.h"

namespace Media::Streaming {

RoundPreview::RoundPreview(const QByteArray &bytes, int size)
: _bytes(bytes)
, _reader(
	Clip::MakeReader(_bytes, [=](Clip::Notification update) {
		clipCallback(update);
	}))
, _size(size) {
}

std::shared_ptr<Ui::DynamicImage> RoundPreview::clone() {
	Unexpected("RoundPreview::clone.");
}

QImage RoundPreview::image(int size) {
	if (!_reader || !_reader->started()) {
		return QImage();
	}
	return _reader->current({
		.frame = QSize(_size, _size),
		.factor = style::DevicePixelRatio(),
		.radius = ImageRoundRadius::Ellipse,
	}, crl::now());
}

void RoundPreview::subscribeToUpdates(Fn<void()> callback) {
	_repaint = std::move(callback);
}

void RoundPreview::clipCallback(Clip::Notification notification) {
	switch (notification) {
	case Clip::Notification::Reinit: {
		if (_reader->state() == ::Media::Clip::State::Error) {
			_reader.setBad();
		} else if (_reader->ready() && !_reader->started()) {
			_reader->start({
				.frame = QSize(_size, _size),
				.factor = style::DevicePixelRatio(),
				.radius = ImageRoundRadius::Ellipse,
			});
		}
	} break;

	case Clip::Notification::Repaint: break;
	}

	if (const auto onstack = _repaint) {
		onstack();
	}
}

} // namespace Media::Streaming
