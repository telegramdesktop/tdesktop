/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video_item_timeline.h"

#include "editor/editor_audio_menu.h"
#include "editor/video/video_clip.h"
#include "editor/video/video_timeline.h"
#include "editor/video/video_timeline_seeker.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_editor.h"

#include <QtGui/QContextMenuEvent>

namespace Editor {

VideoItemTimeline::VideoItemTimeline(not_null<QWidget*> parent)
: RpWidget(parent) {
}

VideoItemTimeline::~VideoItemTimeline() = default;

void VideoItemTimeline::setClip(std::shared_ptr<VideoClip> clip) {
	if (_clip == clip) {
		return;
	}
	_clipLifetime.destroy();
	_menu = nullptr;
	_seeker = nullptr;
	_timeline = nullptr;
	_clip = std::move(clip);
	if (!_clip) {
		return;
	}
	const auto &source = _clip->source();
	const auto trim = _clip->trim();
	_timeline = base::make_unique_q<VideoTimeline>(
		this,
		VideoTimelineDescriptor{
			.path = source->path,
			.content = source->content,
			.dimensions = source->thumbnail.size(),
			.cache = _clip->timelineFrames(),
			.duration = _clip->duration(),
			.from = trim.from,
			.till = trim.till,
			.trimOnly = true,
		});
	_timeline->show();
	_seeker = std::make_unique<TimelineSeeker>(
		_timeline.get(),
		_clip->player());
	_clip->setTrim({ _timeline->from(), _timeline->till() });

	_timeline->trimChanges(
	) | rpl::on_next([=] {
		_clip->setTrim({ _timeline->from(), _timeline->till() });
		_lengthChanges.fire(_clip->loopDuration());
	}, _clipLifetime);

	if (width() > 0) {
		resizeToWidth(width());
	}
}

rpl::producer<crl::time> VideoItemTimeline::lengthChanges() const {
	return _lengthChanges.events();
}

void VideoItemTimeline::refreshTrim() {
	if (_timeline) {
		const auto trim = _clip->trim();
		_timeline->setTrim(trim.from, trim.till);
	}
}

void VideoItemTimeline::commitPendingEdit() {
	if (_timeline) {
		_timeline->commitPendingEdit();
	}
}

void VideoItemTimeline::contextMenuEvent(QContextMenuEvent *e) {
	if (!_clip || !_clip->hasAudio()) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::photoEditorMediaMenu);
	const auto clip = _clip;
	AddVolumeAction(_menu.get(), clip->volume(), [=](float64 volume) {
		clip->setVolume(volume);
	});
	_menu->popup(e->globalPos());
	e->accept();
}

int VideoItemTimeline::resizeGetHeight(int newWidth) {
	const auto height = st::photoEditorButtonBarHeight;
	if (_timeline) {
		_timeline->resizeToWidth(std::max(newWidth, 1));
		_timeline->moveToLeft(0, (height - _timeline->height()) / 2);
	}
	return height;
}

} // namespace Editor
