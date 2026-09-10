/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video_item_timeline.h"

#include "editor/scene/scene_item_video.h"
#include "editor/video/video_timeline.h"
#include "editor/video/video_timeline_seeker.h"
#include "styles/style_editor.h"

namespace Editor {

VideoItemTimeline::VideoItemTimeline(not_null<QWidget*> parent)
: RpWidget(parent) {
}

VideoItemTimeline::~VideoItemTimeline() = default;

void VideoItemTimeline::setItem(std::shared_ptr<ItemVideo> item) {
	if (_item == item) {
		return;
	}
	_itemLifetime.destroy();
	_menu = nullptr;
	_seeker = nullptr;
	_timeline = nullptr;
	_item = std::move(item);
	if (!_item) {
		return;
	}
	const auto &source = _item->source();
	const auto trim = _item->trim();
	_timeline = base::make_unique_q<VideoTimeline>(
		this,
		VideoTimelineDescriptor{
			.path = source.path,
			.content = source.content,
			.dimensions = source.thumbnail.size(),
			.duration = _item->duration(),
			.from = trim.from,
			.till = trim.till,
			.trimOnly = true,
		});
	_timeline->show();
	_seeker = std::make_unique<TimelineSeeker>(
		_timeline.get(),
		_item->player());
	_item->setTrim({ _timeline->from(), _timeline->till() });

	_timeline->trimChanges(
	) | rpl::on_next([=] {
		_item->setTrim({ _timeline->from(), _timeline->till() });
	}, _itemLifetime);

	if (width() > 0) {
		resizeToWidth(width());
	}
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
