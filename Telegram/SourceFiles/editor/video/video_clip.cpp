/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_clip.h"

#include "editor/video/video_segment_player.h"
#include "editor/video/video_timeline.h"
#include "ui/image/image_prepare.h"

namespace Editor {

VideoClip::VideoClip(
	std::shared_ptr<VideoClipSource> source,
	Fn<void()> repaint)
: _source(std::move(source))
, _player(std::make_unique<SegmentPlayer>(
	_source->path,
	_source->content)) {
	_player->repaints(
	) | rpl::on_next(std::move(repaint), _lifetime);
	_player->start();
}

VideoClip::~VideoClip() = default;

const std::shared_ptr<VideoClipSource> &VideoClip::source() const {
	return _source;
}

crl::time VideoClip::duration() const {
	return _source->duration;
}

not_null<SegmentPlayer*> VideoClip::player() const {
	return _player.get();
}

auto VideoClip::timelineFrames()
-> const std::shared_ptr<VideoTimelineFramesCache> & {
	if (!_timelineFrames) {
		_timelineFrames = std::make_shared<VideoTimelineFramesCache>();
	}
	return _timelineFrames;
}

VideoTrim VideoClip::trim() const {
	return _trim;
}

void VideoClip::setTrim(VideoTrim trim) {
	const auto full = duration();
	if (full > 0) {
		trim.from = std::clamp(trim.from, crl::time(0), full);
		trim.till = (trim.till > 0)
			? std::clamp(trim.till, trim.from, full)
			: 0;
	}
	if (_trim == trim) {
		return;
	}
	_trim = trim;
	_player->setSegment(_trim.from, _trim.till);
}

crl::time VideoClip::loopDuration() const {
	const auto full = duration();
	if (full <= 0) {
		return 0;
	}
	const auto till = (_trim.till > _trim.from) ? _trim.till : full;
	return till - _trim.from;
}

bool VideoClip::hasAudio() const {
	return _source->hasAudio;
}

float64 VideoClip::volume() const {
	return _volume;
}

void VideoClip::setVolume(float64 volume) {
	_volume = std::clamp(volume, 0., 1.);
	_player->setVolume(_volume);
}

bool VideoClip::sounding() const {
	return hasAudio() && (_volume > 0.);
}

void VideoClip::setSoundEnabled(bool enabled) {
	_player->setSound(enabled && hasAudio());
}

VideoClip::State VideoClip::state() const {
	return { .trim = _trim, .volume = _volume };
}

void VideoClip::restore(const State &state) {
	setTrim(state.trim);
	setVolume(state.volume);
}

bool VideoClip::animated() const {
	return _player->valid() || _released;
}

bool VideoClip::hasContent() const {
	return !_source->path.isEmpty() || !_source->content.isEmpty();
}

QByteArray VideoClip::content() const {
	if (_source->path.isEmpty()) {
		return _source->content;
	}
	auto file = QFile(_source->path);
	if (file.size() > Images::kReadBytesLimit
		|| !file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll();
}

QImage VideoClip::frame(QSize size) {
	return _player->frame(size);
}

void VideoClip::stop() {
	if (!animated()) {
		return;
	}
	_released = true;
	_stopped = true;
	_player->stop();
}

void VideoClip::resume() {
	if (base::take(_stopped)) {
		_player->start();
	}
}

} // namespace Editor
