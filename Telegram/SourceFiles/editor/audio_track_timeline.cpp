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
		_timeline->removeRequests(
		) | rpl::start_to_stream(_removeRequests, _timeline->lifetime());
		_timeline->trimChanges(
		) | rpl::on_next([=] {
			_lengthChanges.fire(_track->length());
		}, _timeline->lifetime());
		_timeline->show();
		if (width() > 0) {
			resizeToWidth(width());
		}
	}
	_timeline->setPlaying(true);
}

rpl::producer<crl::time> AudioTrackTimeline::lengthChanges() const {
	return _lengthChanges.events();
}

void AudioTrackTimeline::refreshTrim() {
	if (_timeline) {
		_timeline->refreshTrim();
	}
}

void AudioTrackTimeline::commitPendingEdit() {
	if (_timeline) {
		_timeline->commitPendingEdit();
	}
}

void AudioTrackTimeline::refreshVolume() {
	if (_timeline) {
		_timeline->refreshVolume();
	}
}

rpl::producer<> AudioTrackTimeline::removeRequests() const {
	return _removeRequests.events();
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
