/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/audio_track_timeline.h"

#include "editor/editor_audio_timeline.h"
#include "editor/photo_editor_common.h"
#include "styles/style_editor.h"

namespace Editor {

AudioTrackTimeline::AudioTrackTimeline(not_null<QWidget*> parent)
: RpWidget(parent) {
}

AudioTrackTimeline::~AudioTrackTimeline() = default;

void AudioTrackTimeline::setTrack(std::shared_ptr<AudioTrack> track) {
	if (!track) {
		if (_timeline) {
			_timeline->setPlaying(false);
		}
		return;
	} else if (_track != track) {
		_track = std::move(track);
		_timeline = base::make_unique_q<AudioTimeline>(this, _track);
		_timeline->show();
		if (width() > 0) {
			resizeToWidth(width());
		}
	}
	_timeline->setPlaying(true);
}

void AudioTrackTimeline::refreshTrim() {
	if (_timeline) {
		_timeline->refreshTrim();
	}
}

int AudioTrackTimeline::resizeGetHeight(int newWidth) {
	const auto height = st::photoEditorButtonBarHeight;
	if (_timeline) {
		_timeline->resizeToWidth(std::max(newWidth, 1));
		_timeline->moveToLeft(0, (height - _timeline->height()) / 2);
	}
	return height;
}

} // namespace Editor
