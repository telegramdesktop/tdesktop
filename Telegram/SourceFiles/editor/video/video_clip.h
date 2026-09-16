/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/photo_editor_inner_common.h"

namespace Editor {

class SegmentPlayer;
class VideoTimelineFramesCache;

struct VideoClipSource {
	QString path;
	QByteArray content;
	QImage thumbnail;
	crl::time duration = 0;
	bool hasAudio = false;
};

class VideoClip final {
public:
	struct State {
		VideoTrim trim;
		float64 volume = 1.;
	};

	VideoClip(std::shared_ptr<VideoClipSource> source, Fn<void()> repaint);
	~VideoClip();

	[[nodiscard]] const std::shared_ptr<VideoClipSource> &source() const;
	[[nodiscard]] crl::time duration() const;
	[[nodiscard]] not_null<SegmentPlayer*> player() const;
	[[nodiscard]] auto timelineFrames()
		-> const std::shared_ptr<VideoTimelineFramesCache> &;
	[[nodiscard]] VideoTrim trim() const;
	void setTrim(VideoTrim trim);
	[[nodiscard]] crl::time loopDuration() const;
	[[nodiscard]] bool hasAudio() const;
	[[nodiscard]] float64 volume() const;
	void setVolume(float64 volume);
	[[nodiscard]] bool sounding() const;
	void setSoundEnabled(bool enabled);
	[[nodiscard]] State state() const;
	void restore(const State &state);

	[[nodiscard]] bool animated() const;
	[[nodiscard]] bool hasContent() const;
	[[nodiscard]] QByteArray content() const;
	[[nodiscard]] QImage frame(QSize size);
	void stop();
	void resume();

private:
	const std::shared_ptr<VideoClipSource> _source;
	const std::unique_ptr<SegmentPlayer> _player;
	std::shared_ptr<VideoTimelineFramesCache> _timelineFrames;
	VideoTrim _trim;
	float64 _volume = 1.;
	bool _released = false;
	bool _stopped = false;

	rpl::lifetime _lifetime;

};

} // namespace Editor
