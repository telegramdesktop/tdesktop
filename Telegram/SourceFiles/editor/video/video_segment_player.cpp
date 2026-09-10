/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_segment_player.h"

#include "media/streaming/media_streaming_common.h"
#include "media/streaming/media_streaming_document.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_loader_local.h"
#include "media/streaming/media_streaming_player.h"

namespace Editor {
namespace {

[[nodiscard]] Media::Streaming::FrameRequest FrameRequestFor(
		QSize size,
		bool keepAlpha) {
	auto result = Media::Streaming::FrameRequest();
	result.resize = size * style::DevicePixelRatio();
	result.outer = result.resize;
	result.keepAlpha = keepAlpha;
	return result;
}

} // namespace

SegmentPlayer::SegmentPlayer(
	QString path,
	QByteArray content,
	bool keepAlpha)
: _path(std::move(path))
, _content(std::move(content))
, _keepAlpha(keepAlpha) {
}

SegmentPlayer::~SegmentPlayer() = default;

void SegmentPlayer::start() {
	using namespace Media::Streaming;
	if (_instance) {
		return;
	}
	auto loader = _path.isEmpty()
		? MakeBytesLoader(_content)
		: MakeFileLoader(_path);
	if (!loader) {
		return;
	}
	_instance = std::make_unique<Instance>(
		std::make_shared<Document>(std::move(loader)),
		nullptr);
	if (!_instance->valid()) {
		_instance = nullptr;
		return;
	}
	_instance->lockPlayer();
	_instance->player().updates(
	) | rpl::on_next_error([=](Update &&update) {
		handleUpdate(std::move(update));
	}, [=](Error &&) {
		_instance = nullptr;
		_repaints.fire({});
	}, _instance->lifetime());

	restart(_from);
}

bool SegmentPlayer::valid() const {
	return (_instance != nullptr);
}

bool SegmentPlayer::ready() const {
	return _instance
		&& _instance->player().ready()
		&& !_instance->player().videoSize().isEmpty();
}

crl::time SegmentPlayer::segmentTill() const {
	return (_till > _from)
		? _till
		: std::numeric_limits<crl::time>::max();
}

void SegmentPlayer::setSegment(crl::time from, crl::time till) {
	_from = std::max(from, crl::time(0));
	_till = (till > _from) ? till : 0;
	const auto outside = (_position < _from)
		|| (_position >= segmentTill());
	if (outside && !held()) {
		restart(_from);
	}
}

void SegmentPlayer::keepLastFrame() {
	if (!ready() || _frameSize.isEmpty()) {
		return;
	}
	const auto frame = _instance->frame(
		FrameRequestFor(_frameSize, _keepAlpha));
	if (!frame.isNull()) {
		_lastFrame = frame.copy();
	}
}

void SegmentPlayer::restart(crl::time position) {
	if (!_instance) {
		return;
	}
	using namespace Media::Streaming;
	keepLastFrame();
	_position = std::clamp(position, _from, segmentTill());
	auto options = PlaybackOptions();
	options.mode = Mode::Video;
	options.position = _position;
	options.loop = false;
	_instance->play(options);
	if (held()) {
		_instance->pause();
	}
	_positionUpdates.fire_copy(_position);
	_repaints.fire({});
}

void SegmentPlayer::applyHeld() {
	if (!_instance) {
		return;
	} else if (held()) {
		_instance->pause();
	} else {
		_instance->resume();
	}
}

void SegmentPlayer::setPaused(bool paused) {
	if (_paused == paused) {
		return;
	}
	_paused = paused;
	applyHeld();
}

void SegmentPlayer::setSeeking(bool seeking) {
	if (_seeking == seeking) {
		return;
	}
	_seeking = seeking;
	applyHeld();
}

void SegmentPlayer::handleUpdate(Media::Streaming::Update &&update) {
	using namespace Media::Streaming;
	v::match(update.data, [&](Information &) {
		_repaints.fire({});
	}, [&](PreloadedVideo) {
	}, [&](UpdateVideo &data) {
		_position = data.position;
		if (held()) {
			_instance->pause();
			_repaints.fire({});
			return;
		} else if (_position >= segmentTill()) {
			restart(_from);
			return;
		}
		_positionUpdates.fire_copy(_position);
		_repaints.fire({});
	}, [&](PreloadedAudio) {
	}, [&](UpdateAudio) {
	}, [&](WaitingForData) {
	}, [&](SpeedEstimate) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
		restart(_from);
	});
}

QImage SegmentPlayer::frame(QSize size) {
	_frameSize = size;
	if (ready()) {
		auto result = _instance->frame(FrameRequestFor(size, _keepAlpha));
		if (!held()) {
			_instance->markFrameShown();
		}
		if (!result.isNull()) {
			return result;
		}
	}
	return _lastFrame;
}

} // namespace Editor
