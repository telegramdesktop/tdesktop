/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_timeline_seeker.h"

#include "editor/editor_trim_timeline.h"
#include "editor/video/video_segment_player.h"

namespace Editor {
namespace {

constexpr auto kSeekThrottle = crl::time(120);

} // namespace

TimelineSeeker::TimelineSeeker(
	not_null<TrimTimeline*> timeline,
	not_null<SegmentPlayer*> player)
: _timeline(timeline)
, _player(player)
, _timer([=] {
	const auto position = std::exchange(_pending, crl::time(-1));
	if (position >= 0) {
		_player->restart(position);
	}
}) {
	_player->setSegment(_timeline->from(), _timeline->till());

	_timeline->trimChanges(
	) | rpl::on_next([=](crl::time edge) {
		_player->setSegment(_timeline->from(), _timeline->till());
		seek(edge);
	}, _lifetime);

	_timeline->coverChanges(
	) | rpl::on_next([=](crl::time position) {
		seek(position);
	}, _lifetime);

	_timeline->draggingChanges(
	) | rpl::on_next([=](bool dragging) {
		_dragging = dragging;
		if (dragging) {
			_player->setSeeking(true);
		} else {
			finishDragging();
		}
	}, _lifetime);

	_player->positionUpdates(
	) | rpl::on_next([=](crl::time position) {
		if (!_dragging) {
			_timeline->setPlaybackPosition(position);
		}
	}, _lifetime);
}

TimelineSeeker::~TimelineSeeker() {
	if (_dragging) {
		_player->setSeeking(false);
	}
}

void TimelineSeeker::seek(crl::time position) {
	_pending = position;
	if (!_timer.isActive()) {
		_timer.callOnce(kSeekThrottle);
	}
}

void TimelineSeeker::finishDragging() {
	_timer.cancel();
	const auto latest = (_pending >= 0) ? _pending : _player->position();
	_pending = -1;
	const auto from = _timeline->from();
	const auto till = _timeline->till();
	const auto resume = std::clamp(latest, from, till);
	_player->setSeeking(false);
	_player->restart((resume >= till) ? from : resume);
}

} // namespace Editor
